#include <Adafruit_GFX.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <PubSubClient.h>
#include <RTClib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <Wire.h>
#include <WebServer.h>
#include "secrets.h"

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

const char *mqtt_server = MQTT_SERVER;
const int mqtt_port = MQTT_PORT;
const char *mqtt_user = MQTT_USER;
const char *mqtt_pass = MQTT_PASS;

WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);

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

String getTelemetryJSON() {
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
  
  return json;
}

void publishMQTT() {
  if (!mqttClient.connected())
    return;

  String json = getTelemetryJSON();
  mqttClient.publish("hs2026/telemetry", json.c_str());
}

void handleGetInfo() {
  // CORS suport just in case for web interface
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getTelemetryJSON());
}

void handleWifiStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String json = "{";
  json += "\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI());
  json += "}";
  server.send(200, "application/json", json);
}

void handleResetWiFi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Resetting WiFi credentials. ESP will restart and open config portal (AP: HS_Sensor_Setup / 12345678).");
  delay(500);
  WiFiManager wm;
  wm.resetSettings();  // Sterge SSID/parola salvate din NVS
  ESP.restart();
}

void handleWifiPage() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String html = R"rawhtml(
<!DOCTYPE html><html lang='en'><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>WiFi Management - HS Sensor</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Segoe UI',system-ui,sans-serif;background:#0f172a;color:#e2e8f0;min-height:100vh;display:flex;align-items:center;justify-content:center}
  .card{background:#1e293b;border:1px solid #334155;border-radius:16px;padding:32px;width:100%;max-width:420px;box-shadow:0 20px 60px rgba(0,0,0,.5)}
  h1{font-size:1.4rem;font-weight:700;color:#f8fafc;margin-bottom:4px}
  .sub{font-size:.85rem;color:#64748b;margin-bottom:28px}
  .badge{display:inline-block;padding:4px 12px;border-radius:9999px;font-size:.75rem;font-weight:600;margin-bottom:20px}
  .badge.ok{background:#064e3b;color:#6ee7b7}
  .badge.err{background:#450a0a;color:#fca5a5}
  .row{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid #1e293b;font-size:.9rem}
  .row:last-of-type{border:none}
  .label{color:#94a3b8}
  .val{font-weight:600;color:#f1f5f9}
  .btn{display:block;width:100%;margin-top:28px;padding:14px;border:none;border-radius:10px;font-size:1rem;font-weight:600;cursor:pointer;transition:all .2s}
  .btn-danger{background:#dc2626;color:#fff}
  .btn-danger:hover{background:#b91c1c;transform:translateY(-1px)}
  .btn-danger:active{transform:translateY(0)}
  .note{margin-top:16px;font-size:.78rem;color:#475569;text-align:center;line-height:1.5}
  #status-msg{margin-top:16px;padding:12px;border-radius:8px;font-size:.85rem;display:none;text-align:center}
  #status-msg.show{display:block;background:#1c1917;border:1px solid #57534e;color:#d6d3d1}
</style>
</head><body>
<div class='card'>
  <h1>&#127760; WiFi Management</h1>
  <p class='sub'>HS Sensor Node &mdash; Network Control</p>
  <div id='badge' class='badge'>Loading...</div>
  <div class='row'><span class='label'>Network (SSID)</span><span class='val' id='ssid'>-</span></div>
  <div class='row'><span class='label'>IP Address</span><span class='val' id='ip'>-</span></div>
  <div class='row'><span class='label'>Signal (RSSI)</span><span class='val' id='rssi'>-</span></div>
  <button class='btn btn-danger' onclick='doReset()'>&#8635; Change WiFi Network</button>
  <div id='status-msg'></div>
  <p class='note'>ESP will restart and open the config portal.<br>Connect to <b>HS_Sensor_Setup</b> (pwd: <b>12345678</b>) to choose a new network.</p>
</div>
<script>
async function loadStatus(){
  try{
    const r=await fetch('/wifi_status');
    const d=await r.json();
    const b=document.getElementById('badge');
    if(d.connected){b.textContent='Connected';b.className='badge ok';}else{b.textContent='Disconnected';b.className='badge err';}
    document.getElementById('ssid').textContent=d.ssid||'N/A';
    document.getElementById('ip').textContent=d.ip||'N/A';
    document.getElementById('rssi').textContent=d.rssi?d.rssi+' dBm':'N/A';
  }catch(e){console.error(e);}
}
async function doReset(){
  const msg=document.getElementById('status-msg');
  msg.textContent='Sending reset command...';
  msg.className='status-msg show';
  try{
    const r=await fetch('/reset_wifi');
    const t=await r.text();
    msg.textContent='Done! '+t;
    msg.style.display='block';
  }catch(e){
    msg.textContent='ESP is restarting... reconnect to HS_Sensor_Setup AP.';
    msg.style.display='block';
  }
}
loadStatus();
setInterval(loadStatus,5000);
</script>
</body></html>
)rawhtml";
  server.send(200, "text/html", html);
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

  espClient.setInsecure(); // Fara verificare certificat pentru eficienta /
                           // compatibilitate
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512); // Extindere buffer pt JSON lung

  server.on("/get_info", HTTP_GET, handleGetInfo);
  server.on("/wifi_status", HTTP_GET, handleWifiStatus);
  server.on("/reset_wifi", HTTP_GET, handleResetWiFi);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.begin();
  Serial.println(F("HTTP server started"));

  delay(1500);
}

void loop() {
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

  server.handleClient();

  // Yield crucial catre task-ul Idle din FreeRTOS pentru a calcula CPU Load
  // eficient si a nu bloca watchdog-ul
  delay(2);
}
