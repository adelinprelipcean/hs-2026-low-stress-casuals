#include <Adafruit_GFX.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>

extern "C" {
#include "esp_freertos_hooks.h"
}

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
#define I2C_SDA_PIN 5
#define I2C_SCL_PIN 6
#define BTN1_PIN 2
#define BTN2_PIN 3

// ---------------------------------------------------------
// Peripheral Objects
// ---------------------------------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_INA219 ina219;
RTC_DS3231 rtc;
WebServer server(80);

const char *mqtt_server = "13c35f0a5bde4564be3ea561c26c7c3b.s1.eu.hivemq.cloud";
const int mqtt_port = 8883;
const char *mqtt_user = "esp32mqtt";
const char *mqtt_pass = "L7sqBG*9+w2m";

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);

#define PCF8591_ADDR 0x48

// ---------------------------------------------------------
// Calibrare Hardware
// Modulul HW-011 foloseste rezistente si termistori cu toleranta mare (5-10%).
// Aceasta valoare se aduna la temperatura calculata brut pentru a o alinia cu
// un termostat. Ex: Daca ESP arata 25.5 si camera are 20.5, offset-ul este
// -5.0f
// ---------------------------------------------------------
#define TEMP_CALIBRATION_OFFSET -5.0f

// ---------------------------------------------------------
// Telemetry State
// ---------------------------------------------------------
float g_temperature = 0;
float g_lightLux = 0;
float g_voltage = 0;
float g_current = 0;
float g_avgCurrent = 0; // Pentru a stabiliza "Time Left"
float g_totalMah = 0;
float g_cpuLoad = 0;
char g_timestamp[32] = "--:--:--";
uint8_t g_displayPage = 0;
#define MAX_PAGES 4

// Stocare status brut pini
uint8_t g_rawAIN0 = 0;
uint8_t g_rawAIN1 = 0;
uint8_t g_rawAIN2 = 0;
uint8_t g_rawAIN3 = 0;
bool g_btn1State = false;
bool g_btn2State = false;

// ---------------------------------------------------------
// CPU Load (FreeRTOS Idle Hook)
// ---------------------------------------------------------
volatile uint32_t idleCounter = 0;
uint32_t maxIdleCount = 0;

bool idle_task_hook() {
  idleCounter = idleCounter + 1; // Fix for C++20 volatile deprecated warning
  return true;
}

// ---------------------------------------------------------
// PCF8591 Raw I2C Read (Evita ESP32 Repeated Start Bug)
// ---------------------------------------------------------
uint8_t readPCF8591(uint8_t channel) {
  uint8_t configByte = 0x40 | (channel & 0x03);
  Wire.beginTransmission(PCF8591_ADDR);
  Wire.write(configByte);
  Wire.endTransmission(true); // Stop fortat
  delay(2);
  Wire.requestFrom(PCF8591_ADDR, 2);
  if (Wire.available() >= 2) {
    Wire.read();
    return Wire.read();
  }
  return 255;
}

// ---------------------------------------------------------
// Sensor Read (called 1Hz)
// ---------------------------------------------------------
void readSensors() {
  uint32_t curIdle = idleCounter;
  idleCounter = 0;

  if (curIdle > maxIdleCount) {
    maxIdleCount = curIdle; // Auto-calibrate idle max based on true sleep ticks
  }

  float load = (maxIdleCount > 0)
                   ? 100.0f * (1.0f - ((float)curIdle / maxIdleCount))
                   : 0.0f;
  g_cpuLoad = constrain(load, 0.0f, 100.0f);

  // --- PCF8591 ---
  delay(10);
  g_rawAIN0 = readPCF8591(0); // AIN0 = Fotorezistenta (LDR)
  delay(2);
  g_rawAIN1 = readPCF8591(1); // AIN1 = Termistorul NTC! (0x41)
  delay(2);
  g_rawAIN2 = readPCF8591(2); // AIN2 libere pentru alte instrumente
  delay(2);
  g_rawAIN3 = readPCF8591(3); // AIN3 liberate pentru alte instrumente

  uint8_t rawLDR = g_rawAIN0;
  uint8_t rawNTC = g_rawAIN1;

  // Calculare Iluminare Lux conform model HW-011 si LDR GL5528
  uint8_t safeRawLDR = (rawLDR == 0) ? 1 : ((rawLDR >= 255) ? 254 : rawLDR);
  float voutLdr = safeRawLDR * (3.3f / 255.0f);
  float rLdr = (10000.0f * voutLdr) /
               (3.3f - voutLdr); // LDR legat la masa cu pull-up 10k
  g_lightLux = 500000.0f /
               rLdr; // Aproximare Lux (depinde de curba logaritmica a LDR-ului)

  // Matematica NTC (Acel '-18C' dovedeste 100% ca ai un termistor de 100k, nu
  // de 10k!)
  uint8_t tmpNTC = (rawNTC == 0) ? 1 : ((rawNTC >= 255) ? 254 : rawNTC);

  // Rezistorul divizor de pe placuta (SMD) este de 10k
  float resistance = 10000.0f * ((float)tmpNTC / (255.0f - (float)tmpNTC));

  // R_0 pentru acest termistor este de fapt 100,000 ohmi (100k) la 25C!
  float steinhart =
      logf(resistance / 100000.0f) / 3950.0f + 1.0f / (25.0f + 273.15f);
  steinhart = 1.0f / steinhart - 273.15f; // Celsius brut

  // Aplicam Offset-ul de Calibrare pentru compensarea erorilor pieselor fizice
  steinhart += TEMP_CALIBRATION_OFFSET;

  g_temperature =
      (steinhart < -40.0f || steinhart > 125.0f) ? -99.0f : steinhart;

  // --- INA219 ---
  delay(5);
  g_voltage = ina219.getBusVoltage_V();
  g_current = ina219.getCurrent_mA();

  // Filtru EMA (Exponential Moving Average) foarte lent (Alpha = 0.05) ca sa nu
  // sara "Time left" orbeste.
  if (g_avgCurrent == 0)
    g_avgCurrent = g_current;
  else
    g_avgCurrent = (g_avgCurrent * 0.95f) + (g_current * 0.05f);

  g_totalMah += g_avgCurrent * (1.0f / 3600.0f);

  // --- RTC ---
  delay(5);
  DateTime now = rtc.now();
  snprintf(g_timestamp, sizeof(g_timestamp), "%02d:%02d:%02d", now.hour(),
           now.minute(), now.second());
}

// ---------------------------------------------------------
// Display Core
// ---------------------------------------------------------
void drawWiFiBars(int16_t x, int16_t y) {
  if (WiFi.status() == WL_CONNECTED) {
    int rssi = WiFi.RSSI();
    uint8_t bars = 0;
    if (rssi > -65)
      bars = 4;
    else if (rssi > -75)
      bars = 3;
    else if (rssi > -85)
      bars = 2;
    else if (rssi > -95)
      bars = 1;

    for (uint8_t i = 0; i < 4; i++) {
      int16_t h = (i + 1) * 2;
      if (i < bars) {
        display.fillRect(x + i * 3, y + 8 - h, 2, h, SSD1306_BLACK);
      } else {
        // Un drawRect de latime 2 pixeli este de fapt la fel ca un fillRect.
        // Pentru empty bars, desenam doar baza (un punct / o linie scurta la
        // fund).
        display.drawLine(x + i * 3, y + 7, x + i * 3 + 1, y + 7, SSD1306_BLACK);
      }
    }
  } else {
    display.drawLine(x, y + 2, x + 8, y + 8, SSD1306_BLACK);
    display.drawLine(x, y + 8, x + 8, y + 2, SSD1306_BLACK);
  }
}

void drawMinimalistLogo() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(46, 10);
  display.print("LOW");
  display.setCursor(28, 30);
  display.print("STRESS");
  display.setTextSize(1);
  display.setCursor(43, 52);
  display.print("CASUALS");
  display.display();
}

void drawHeader(const char *title) {
  display.fillRect(0, 0, 128, 13, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  int16_t x = 64 - (strlen(title) * 6) / 2;
  display.setCursor(x, 3);
  display.print(title);

  // Draw WiFi icon in top right corner
  drawWiFiBars(114, 3);

  display.setTextColor(SSD1306_WHITE);
}

void updateDisplay() {
  display.clearDisplay();

  if (g_displayPage == 0) {
    drawHeader("ENVIRONMENT");
    display.setTextSize(1);
    display.setCursor(5, 20);
    display.print("Temp:");
    display.setTextSize(2);
    display.setCursor(45, 16);
    display.print(g_temperature, 1);
    display.setTextSize(1);
    display.print(" C");

    display.drawLine(5, 36, 123, 36, SSD1306_WHITE);

    display.setCursor(5, 45);
    display.print("Light:");
    display.setTextSize(2);
    display.setCursor(45, 41);
    display.print((int)g_lightLux);
    display.setTextSize(1);
    display.print(" lx");

  } else if (g_displayPage == 1) {
    drawHeader("18650 MH1 BATTERY");
    display.setTextSize(1);

    // Tensiune si Curent
    display.setCursor(4, 20);
    display.print("V:");
    display.setCursor(20, 20);
    display.print(g_voltage, 2);
    display.print("v");
    display.setCursor(68, 20);
    display.print("I:");
    display.setCursor(84, 20);
    display.print(g_current, 1);
    display.print("mA");

    display.drawLine(5, 31, 123, 31, SSD1306_WHITE);

    // Calcul % Baterie INR18650MH1 (LG 3200mAh)
    // Curba standard: 4.2V (100%) - 3.0V (0% pentru o utilizare sigura pe
    // ESP32)
    float batPercent = (g_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    batPercent = constrain(batPercent, 0.0f, 100.0f);

    // Afisare procent
    display.setCursor(5, 38);
    display.print("Bat:");
    display.print((int)batPercent);
    display.print("%");

    // Pictograma baterie integrata
    display.drawRect(70, 36, 40, 12, SSD1306_WHITE);
    display.fillRect(110, 39, 3, 6,
                     SSD1306_WHITE); // "Varful" bateriei (+ nipple)
    // Fill interior in functie de procent (latime maxima 36 pixeli)
    uint8_t fillW = map((int)batPercent, 0, 100, 0, 36);
    display.fillRect(72, 38, fillW, 8, SSD1306_WHITE);

    // Estimare Timp Ramas (Capacitate totala celula: 3200mAh)
    display.setCursor(5, 52);
    if (g_avgCurrent > 2.0f || g_avgCurrent < -2.0f) {
      // Cat timp mai tine la curentul MEDIU FILTRAT
      float hours = (3200.0f * (batPercent / 100.0f)) / fabs(g_avgCurrent);
      int h = (int)hours;
      int m = (int)((hours - h) * 60);

      if (h > 99)
        display.print("Time left: >99h");
      else
        display.printf("Time left: %02dh %02dm", h, m);
    } else {
      display.print("Time left: ---");
    }

  } else if (g_displayPage == 2) {
    drawHeader("SYSTEM STATS");
    display.setTextSize(1);
    display.setCursor(5, 22);
    display.print("Time: ");
    display.print(g_timestamp);
    display.setCursor(5, 38);
    display.print("CPU Load:");

    display.drawRect(65, 38, 55, 9, SSD1306_WHITE);
    uint8_t barW = map((int)g_cpuLoad, 0, 100, 0, 51);
    display.fillRect(67, 40, barW, 5, SSD1306_WHITE);

    display.setCursor(5, 52);
    display.print(g_cpuLoad, 1);
    display.print(" %");
  } else if (g_displayPage == 3) {
    drawHeader("WIFI NETWORK");
    display.setTextSize(1);
    display.setCursor(4, 20);

    if (WiFi.status() == WL_CONNECTED) {
      display.print("SSID:");
      String ssid = WiFi.SSID();
      if (ssid.length() > 14) {
        ssid = ssid.substring(0, 11) + "...";
      }
      display.println(ssid);

      display.setCursor(4, 32);
      display.print("IP:  ");
      display.println(WiFi.localIP());

      display.setCursor(4, 44);
      display.print("RSSI:");

      int rssiPercent = map(WiFi.RSSI(), -100, -50, 0, 100);
      rssiPercent = constrain(rssiPercent, 0, 100);

      display.setCursor(38, 44);
      display.print(WiFi.RSSI());
      display.print("dBm (");
      display.print(rssiPercent);
      display.print("%)");

      // Draw slider pe OLED (grafic procentaj WiFi)
      display.drawRect(4, 53, 120, 5, SSD1306_WHITE);
      uint8_t barW = map(rssiPercent, 0, 100, 0, 116);
      if (barW > 0) {
        display.fillRect(6, 55, barW, 1, SSD1306_WHITE);
      }
    } else {
      display.setCursor(25, 30);
      display.println("Disconnected.");
    }
  }

  // Deseneaza cerculetele de navigare jos (centrate pentru 4 pagini -> latime
  // totala 30px -> start la 49px)
  for (uint8_t i = 0; i < MAX_PAGES; i++) {
    if (i == g_displayPage)
      display.fillCircle(49 + (i * 10), 61, 2, SSD1306_WHITE);
    else
      display.drawCircle(49 + (i * 10), 61, 2, SSD1306_WHITE);
  }
  display.display();
}

void pollButtons() {
  static bool lastBtn1 = HIGH;
  static bool lastBtn2 = HIGH;
  bool btn1 = digitalRead(BTN1_PIN);
  bool btn2 = digitalRead(BTN2_PIN);

  g_btn1State = (btn1 == LOW);
  g_btn2State = (btn2 == LOW);

  if (btn1 == LOW && lastBtn1 == HIGH)
    g_displayPage = (g_displayPage + 1) % MAX_PAGES;
  if (btn2 == LOW && lastBtn2 == HIGH)
    g_displayPage =
        (g_displayPage == 0) ? (MAX_PAGES - 1) : (g_displayPage - 1);
  lastBtn1 = btn1;
  lastBtn2 = btn2;
}

void handleRoot() {
  String html = "<html><head><meta name='viewport' "
                "content='width=device-width, initial-scale=1.0'>";
  html += "<style>body{font-family: Arial, sans-serif; text-align: center; "
          "background-color: #f0f2f5; margin-top: 30px;}";
  html += ".card{background: white; max-width: 400px; margin: auto; padding: "
          "25px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.1);}";
  html += "h1{color: #333;} h3{color: #666;} .data{font-size: 1.2em; "
          "font-weight: bold; color: #007bff;}</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Low Stress Casuals</h1>";
  html += "<h3>Hardware Monitor Status</h3><hr>";
  html += "<p>Temperature: <span class='data'>" + String(g_temperature, 1) +
          " &deg;C</span></p>";
  html += "<p>Illuminance: <span class='data'>" + String((int)g_lightLux) +
          " Lux</span></p>";
  html += "<p>Battery: <span class='data'>" + String(g_voltage, 2) + " V (" +
          String(g_current, 1) + " mA)</span></p>";
  html += "<p>CPU Load: <span class='data'>" + String(g_cpuLoad, 1) +
          " %</span></p>";

  int rssiPercent = map(WiFi.RSSI(), -100, -50, 0, 100);
  rssiPercent = constrain(rssiPercent, 0, 100);
  html += "<p>WiFi Signal: <span class='data'>" + String(WiFi.RSSI()) +
          " dBm (" + String(rssiPercent) + "%)</span></p>";
  html += "<div style='width: 100%; height: 12px; background: #e0e0e0; "
          "border-radius: 6px; overflow: hidden; margin-top: 5px;'>";
  html +=
      "<div style='width: " + String(rssiPercent) +
      "%; height: 100%; background: #4CAF50; border-radius: 6px;'></div></div>";

  html += "<p style='margin-top:20px; font-size: 0.9em; color:#888;'>Time: " +
          String(g_timestamp) + "</p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleApiData() {
  String json = "{";
  json += "\"time\":\"" + String(g_timestamp) + "\",";
  json += "\"cpu_load_pct\":" + String(g_cpuLoad, 1) + ",";
  json += "\"wifi_rssi_dbm\":" + String(WiFi.RSSI()) + ",";

  json += "\"pcf8591_ain0_raw\":" + String(g_rawAIN0) + ",";
  json += "\"pcf8591_ain1_raw\":" + String(g_rawAIN1) + ",";
  json += "\"pcf8591_ain2_raw\":" + String(g_rawAIN2) + ",";
  json += "\"pcf8591_ain3_raw\":" + String(g_rawAIN3) + ",";

  json += "\"sensor_light_lux\":" + String(g_lightLux, 1) + ",";
  json += "\"sensor_temperature_c\":" + String(g_temperature, 1) + ",";

  json += "\"ina219_voltage_v\":" + String(g_voltage, 3) + ",";
  json += "\"ina219_current_ma\":" + String(g_current, 1) + ",";
  json += "\"ina219_total_mah\":" + String(g_totalMah, 2) + ",";

  json += "\"digital_btn1_pressed\":" + String(g_btn1State ? "true" : "false") +
          ",";
  json += "\"digital_btn2_pressed\":" + String(g_btn2State ? "true" : "false") +
          ",";

  json += "\"io_ports\":{";
  json += "\"GPIO0\":" + String(digitalRead(0)) + ",";
  json += "\"GPIO1\":" + String(digitalRead(1)) + ",";
  json += "\"GPIO2\":" + String(digitalRead(2)) + ",";
  json += "\"GPIO3\":" + String(digitalRead(3)) + ",";
  json += "\"GPIO4\":" + String(digitalRead(4)) + ",";
  json += "\"GPIO5\":" + String(digitalRead(5)) + ",";
  json += "\"GPIO6\":" + String(digitalRead(6)) + ",";
  json += "\"GPIO7\":" + String(digitalRead(7)) + ",";
  json += "\"GPIO8\":" + String(digitalRead(8)) + ",";
  json += "\"GPIO9\":" + String(digitalRead(9)) + ",";
  json += "\"GPIO10\":" + String(digitalRead(10)) + ",";
  json += "\"GPIO20\":" + String(digitalRead(20)) + ",";
  json += "\"GPIO21\":" + String(digitalRead(21));
  json += "}";

  json += "}";

  server.send(200, "application/json", json);
}

void reconnectMQTT() {
  if (WiFi.status() != WL_CONNECTED)
    return;

  static uint32_t lastMqttAttempt = 0;
  if (millis() - lastMqttAttempt < 5000)
    return;
  lastMqttAttempt = millis();

  if (!mqttClient.connected()) {
    Serial.print(F("Attempting MQTT connection..."));
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println(F("connected"));
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(mqttClient.state());
    }
  }
}

void publishMQTT() {
  if (!mqttClient.connected())
    return;

  float batPercent = (g_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
  batPercent = constrain(batPercent, 0.0f, 100.0f);
  String batLifeStr = "N/A";
  if (fabs(g_avgCurrent) > 2.0f) {
    float hours = (3200.0f * (batPercent / 100.0f)) / fabs(g_avgCurrent);
    int h = (int)hours;
    int m = (int)((hours - h) * 60);
    if (h > 99)
      batLifeStr = ">99h";
    else {
      char buf[16];
      snprintf(buf, sizeof(buf), "%02dh %02dm", h, m);
      batLifeStr = buf;
    }
  }

  DateTime now = rtc.now();
  char isoTime[32];
  snprintf(isoTime, sizeof(isoTime), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           now.year(), now.month(), now.day(), now.hour(), now.minute(),
           now.second());

  String ioLog = "AIN0:" + String(g_rawAIN0) + ",AIN1:" + String(g_rawAIN1) +
                 ",AIN2:" + String(g_rawAIN2) + ",AIN3:" + String(g_rawAIN3) +
                 ",BTN1:" + String(g_btn1State) +
                 ",BTN2:" + String(g_btn2State);

  String json = "{";
  json += "\"temperature\":" + String(g_temperature, 1) + ",";
  json += "\"light_intensity\":" + String(g_lightLux, 1) + ",";
  json += "\"io_log\":\"" + ioLog + "\",";
  json += "\"timestamp\":\"" + String(isoTime) + "\",";
  json += "\"gpio_pin\":\"ALL_PINS\",";
  json += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"network_name\":\"" + WiFi.SSID() + "\",";
  json += "\"cpu_load\":" + String(g_cpuLoad, 1) + ",";
  json += "\"voltage\":" + String(g_voltage, 2) + ",";
  json += "\"current_now\":" + String(g_current, 1) + ",";
  json += "\"current_total\":" + String(g_totalMah, 2) + ",";
  json += "\"battery_life\":\"" + batLifeStr + "\"";
  json += "}";

  mqttClient.publish("hs2026/telemetry", json.c_str());
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Masuram "idleCount" brut, de referinta baza inainte ca librariile de WiFi
  // sa inceapa pachetele pe background.
  esp_register_freertos_idle_hook(idle_task_hook);
  delay(1000);
  maxIdleCount = idleCounter;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println(F("SSD1306 error!"));
  drawMinimalistLogo();
  delay(2000); // Tine logo-ul LOW STRESS CASUALS activ timp de 2 secunde

  if (!ina219.begin())
    Serial.println(F("INA219 error!"));

  if (!rtc.begin()) {
    Serial.println(F("RTC error!"));
  } else {
    // Sincronizeaza ceasul hardware cu ora exacta la care a fost compilat codul
    // pe PC. Pune pe comentariu aceasta linie DUPA primul upload, ca sa nu dea
    // reset la timp decat daca se pierde bateria RTC CR2032!
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // --- WiFiManager Setup ---
  WiFi.mode(WIFI_STA); // Explicitly set mode to avoid ESP32 AP glitches
  WiFi.setTxPower(
      WIFI_POWER_8_5dBm); // Prevent brown-outs on C3 when starting AP
  delay(100);

  display.clearDisplay();

  // Modern WiFi Setup UI
  display.drawRoundRect(0, 0, 128, 64, 4, SSD1306_WHITE);
  display.fillRoundRect(0, 0, 128, 14, 4, SSD1306_WHITE);
  // Squared interior bottom corners
  display.fillRect(0, 10, 128, 4, SSD1306_WHITE);

  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(32, 3);
  display.print(F("WIFI CONFIG"));

  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8, 22);
  display.print(F("AP:  HS_Sensor_Setup"));

  display.setCursor(8, 36);
  display.print(F("PWD: 12345678"));

  display.drawLine(4, 49, 124, 49, SSD1306_WHITE);

  display.setCursor(12, 53);
  display.print(F("Waiting configure."));

  display.display();

  WiFiManager wm;
  // Optional: Set timeout so it doesn't block forever if user doesn't setup
  wm.setConfigPortalTimeout(180); // 3 minutes timeout

  // Folosim WPA2 cu parola deoarece unele telefoane refuza Open AP fara
  // internet
  if (!wm.autoConnect("HS_Sensor_Setup", "12345678")) {
    Serial.println(F("Failed to connect and hit timeout"));
    delay(3000);
  } else {
    Serial.println(F("WiFi connected!"));
  }
  // -------------------------

  server.on("/", handleRoot);
  server.on("/api/data", handleApiData);
  server.begin();

  espClient.setInsecure(); // Fara verificare certificat pentru eficienta /
                           // compatibilitate
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512); // Extindere buffer pt JSON lung

  delay(1500);
}

void loop() {
  server.handleClient();

  if (!mqttClient.connected()) {
    reconnectMQTT();
  }
  mqttClient.loop();

  uint32_t now = millis();
  static uint32_t lastSensorMs = 0;
  static uint32_t lastDisplayMs = 0;

  if (now - lastSensorMs >= 1000) {
    lastSensorMs = now;
    readSensors();
    publishMQTT();
  }

  if (now - lastDisplayMs >= 100) {
    lastDisplayMs = now;
    pollButtons();
    updateDisplay();
  }
}
