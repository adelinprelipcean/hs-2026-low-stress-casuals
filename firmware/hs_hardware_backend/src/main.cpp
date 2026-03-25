#include "secrets.h"
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
#include <esp_wifi.h>

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
// Hardware Calibration
// Temperature offset to compensate for component tolerances
// ---------------------------------------------------------
#define TEMP_CALIBRATION_OFFSET -1.2f

// ---------------------------------------------------------
// Telemetry State
// ---------------------------------------------------------
float g_temperature = 0;
float g_lightLux = 0;
float g_voltage = 0;
float g_current = 0;
float g_avgCurrent = 0; // Stabilizes "Time Left" calculation
float g_totalMah = 0;
bool g_isBatteryCritical = false;
#define BATTERY_CRITICAL_THRESHOLD 3.1f
float g_cpuLoad = 0;
char g_timestamp[32] = "--:--:--";
uint8_t g_displayPage = 0;
#define MAX_PAGES 5

// Raw pin status
uint8_t g_rawAIN0 = 0;
uint8_t g_rawAIN1 = 0;
uint8_t g_rawAIN2 = 0;
uint8_t g_rawAIN3 = 0;
bool g_btn1State = false;
bool g_btn2State = false;

// ---------------------------------------------------------
// Logging System
String g_logBuffer = "";
void addLog(String msg) {
  String line = String(g_timestamp) + " " + msg + "\n";
  g_logBuffer += line;
  if (g_logBuffer.length() > 6000) {
    g_logBuffer = g_logBuffer.substring(1000);
  }
}

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
// PCF8591 Raw I2C Read (Avoids ESP32 Repeated Start Bug)
// ---------------------------------------------------------
uint8_t readPCF8591(uint8_t channel) {
  uint8_t configByte = 0x40 | (channel & 0x03);
  Wire.beginTransmission(PCF8591_ADDR);
  Wire.write(configByte);
  
  // Daca endTransmission returneaza altceva decat 0, inseamna ca bus-ul e blocat
  if (Wire.endTransmission(true) != 0) {
    Serial.println("I2C Bus Error la PCF8591!");
    return 255; 
  }
  
  delay(5); // Timp pentru conversie ADC
  
  uint8_t count = Wire.requestFrom(PCF8591_ADDR, (uint8_t)2);
  if (count >= 2) {
    Wire.read(); // Ignoram primul byte (vechi)
    return Wire.read(); // Returnam valoarea actuala
  }
  return 255;
}

float calculateTemperature(uint8_t rawNTC) {
  if (rawNTC == 255 || rawNTC == 0) return -99.0f;
  uint8_t tmpNTC = (rawNTC >= 255) ? 254 : rawNTC;
  
  // Divider resistor (SMD) is 10k
  float ratio = (float)tmpNTC / (255.0f - (float)tmpNTC);
  float resistance = 10000.0f * ratio;

  // R_0 factor at 25C is 100k, Beta is 3950
  float steinhart = logf(resistance / 100000.0f) / 3950.0f + 1.0f / (25.0f + 273.15f);
  steinhart = 1.0f / steinhart - 273.15f; // Raw Celsius
  
  steinhart += TEMP_CALIBRATION_OFFSET;
  return (steinhart < -40.0f || steinhart > 125.0f) ? -99.0f : steinhart;
}

float calculateLux(uint8_t rawLDR) {
  uint8_t safeRawLDR = (rawLDR == 0) ? 1 : ((rawLDR >= 255) ? 254 : rawLDR);
  float voutLdr = safeRawLDR * (3.3f / 255.0f);
  // Divider formula: rLdr is between 3.3V and signal, 10k is between signal and GND
  float rLdr = (10000.0f * voutLdr) / (3.3f - voutLdr);
  if (rLdr < 1.0f) rLdr = 1.0f;

  // Calibrated constant: 10,000,000 matches typical 150-200 Lux in lit rooms
  float lux = 10000000.0f / rLdr;
  return constrain(lux, 0.0f, 60000.0f);
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
  
  // Lux Oversampling & Smoothing
  float luxSum = 0;
  uint16_t rawLdrSum = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t r = readPCF8591(0); // AIN0: LDR
    luxSum += calculateLux(r);
    rawLdrSum += r;
    delay(2);
  }
  g_rawAIN0 = rawLdrSum / 8;
  float instantLux = luxSum / 8.0f;
  // EMA Filter for Lux
  if (g_lightLux <= 0) g_lightLux = instantLux;
  else g_lightLux = (g_lightLux * 0.8f) + (instantLux * 0.2f);

  delay(5);

  // Temperature Oversampling & Smoothing
  float tempSum = 0;
  uint16_t rawSum = 0;
  uint8_t validSamples = 0;
  for (int i = 0; i < 8; i++) {
    uint8_t r = readPCF8591(1);
    float t = calculateTemperature(r);
    if (t > -90.0f) {
      tempSum += t;
      rawSum += r;
      validSamples++;
    }
    delay(2);
  }
  
  if (validSamples > 0) {
    g_rawAIN1 = rawSum / validSamples;
    float instantTemp = tempSum / validSamples;
    // EMA Filter: alpha = 0.2 (low pass)
    if (g_temperature < -90.0f) g_temperature = instantTemp;
    else g_temperature = (g_temperature * 0.8f) + (instantTemp * 0.2f);
  } else {
    g_rawAIN1 = 255;
    g_temperature = -99.0f;
  }

  g_rawAIN2 = readPCF8591(2);
  delay(2);
  g_rawAIN3 = readPCF8591(3);
  delay(2);

  // --- INA219 ---
  delay(5);
  g_voltage = ina219.getBusVoltage_V();
  g_current = ina219.getCurrent_mA();

  // Low-pass EMA Filter (Alpha = 0.05) to stabilize "Time left" estimation
  if (g_avgCurrent == 0)
    g_avgCurrent = g_current;
  else
    g_avgCurrent = (g_avgCurrent * 0.95f) + (g_current * 0.05f);

  g_totalMah += g_avgCurrent * (1.0f / 3600.0f);

  // --- Protection Check ---
  // If voltage is realistically low but above noise (0.5V), trigger shutdown
  if (g_voltage > 0.5f && g_voltage < BATTERY_CRITICAL_THRESHOLD) {
      g_isBatteryCritical = true;
  }

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
        // Draw base line for empty bars
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

  if (g_isBatteryCritical) {
    display.fillRect(0, 0, 128, 64, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(2);
    display.setCursor(15, 10);
    display.print("CRITICAL");
    display.setCursor(20, 30);
    display.print("BATTERY");
    display.setTextSize(1);
    display.setCursor(15, 50);
    display.print("SHUTTING DOWN...");
    display.display();
    return;
  }

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

    // Voltage and Current
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

    // Battery % for INR18650MH1 (LG 3200mAh)
    // Safe range: 3.0V (0%) to 4.2V (100%)
    float batPercent = (g_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
    batPercent = constrain(batPercent, 0.0f, 100.0f);

    // Display percentage
    display.setCursor(5, 38);
    display.print("Bat:");
    display.print((int)batPercent);
    display.print("%");

    // Battery icon
    display.drawRect(70, 36, 40, 12, SSD1306_WHITE);
    display.fillRect(110, 39, 3, 6,
                     SSD1306_WHITE); // Positive terminal
    // Capacity fill (max width 36px)
    uint8_t fillW = map((int)batPercent, 0, 100, 0, 36);
    display.fillRect(72, 38, fillW, 8, SSD1306_WHITE);

    // Remaining time estimation (Cell capacity: 3200mAh)
    display.setCursor(5, 52);
    if (g_avgCurrent > 2.0f || g_avgCurrent < -2.0f) {
      // Estimated hours using filtered average current
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
    // 1. Header (High-contrast with subtle heartbeat)
    display.fillRect(0, 0, 128, 12, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(18, 2);
    display.print("REAL-TIME LOGS");
    
    // WiFi Signal Icon on the right of the header
    if (WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        int bars = map(constrain(rssi, -100, -50), -100, -50, 1, 4);
        for (int i = 0; i < 4; i++) {
            uint8_t h = 2 + (i * 2);
            if (i < bars) 
              display.fillRect(115 + (i * 3), 9 - h, 2, h, SSD1306_BLACK);
            else 
              display.drawPixel(115 + (i * 3), 9, SSD1306_BLACK); // Base dot for empty bars
        }
    } else {
        display.setCursor(118, 2);
        display.print("x");
    }

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // 2. Timestamp
    display.setCursor(0, 16);
    display.print("TIME:  "); 
    display.print(g_timestamp);

    // 3. Raw I/O Data (Fixed horizontal fit)
    display.setCursor(0, 26);
    display.printf("RAW: A0:%03d  A1:%03d", g_rawAIN0, g_rawAIN1); 
    display.setCursor(0, 35);
    display.printf("     A2:%03d  A3:%03d", g_rawAIN2, g_rawAIN3); 

    // 4. Separator
    display.drawLine(0, 46, 127, 46, SSD1306_WHITE);

    // 5. Processed Data
    display.setCursor(0, 50);
    display.printf("TMP:%2.1fC | LUM:%dlx", g_temperature, (int)g_lightLux);
  } else if (g_displayPage == 3) {
    drawHeader("WIFI NETWORK");
    display.setTextSize(1);
    display.setCursor(4, 20);

    if (WiFi.status() == WL_CONNECTED) {
      display.print("SSID: ");
      display.println(WiFi.SSID());

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

      // Draw WiFi signal percentage bar
      display.drawRect(4, 53, 120, 5, SSD1306_WHITE);
      uint8_t barW = map(rssiPercent, 0, 100, 0, 116);
      if (barW > 0) {
        display.fillRect(6, 55, barW, 1, SSD1306_WHITE);
      }
    }
  } else if (g_displayPage == 4) {
    drawHeader("SYSTEM STATS");
    display.setTextSize(1);
    display.setCursor(4, 20);
    display.print("CPU Load:");
    display.setCursor(64, 20);
    display.print(g_cpuLoad, 1);
    display.print("%");
    
    display.drawLine(5, 36, 123, 36, SSD1306_WHITE);
    
    display.setCursor(4, 45);
    display.print("Time: ");
    display.print(g_timestamp);
  }

  // Draw page navigation indicators (centered, MAX_PAGES pages)
  int startX = 64 - (MAX_PAGES * 10) / 2 + 5;
  for (uint8_t i = 0; i < MAX_PAGES; i++) {
    if (i == g_displayPage)
      display.fillCircle(startX + (i * 10), 61, 2, SSD1306_WHITE);
    else
      display.drawCircle(startX + (i * 10), 61, 2, SSD1306_WHITE);
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

  if (btn1 == LOW && lastBtn1 == HIGH) {
    g_displayPage = (g_displayPage == 0) ? (MAX_PAGES - 1) : (g_displayPage - 1);
    addLog("Page Prev: " + String(g_displayPage));
  }
  if (btn2 == LOW && lastBtn2 == HIGH) {
    g_displayPage = (g_displayPage + 1) % MAX_PAGES;
    addLog("Page Next: " + String(g_displayPage));
  }
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
    addLog("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      addLog("connected");
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
  json += "\"battery_percentage\":" + String(batPercent, 1) + ",";
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
  if (mqttClient.publish("hs2026/telemetry", json.c_str())) {
    char buf[48];
    // Log as: IO BTNS A0 A1 A2 A3
    snprintf(buf, sizeof(buf), "IO %d%d %03d %03d %03d %03d", g_btn1State,
             g_btn2State, g_rawAIN0, g_rawAIN1, g_rawAIN2, g_rawAIN3);
    addLog(buf);
  } else {
    addLog("MQTT ERR");
  }
}

void handleGetInfo() {
  // CORS suport just in case for web interface
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", getTelemetryJSON());
}

void handleWifiStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String json = "{";
  json += "\"connected\":" +
          String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ssid\":\"" + WiFi.SSID() + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"rssi\":" + String(WiFi.RSSI());
  json += "}";
  server.send(200, "application/json", json);
}

void handleResetWiFi() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain",
              "Resetting WiFi credentials. ESP will restart and open config "
              "portal (AP: HS_Sensor_Setup / 12345678).");
  delay(500);
  WiFiManager wm;
  wm.resetSettings(); // Sterge SSID/parola salvate din NVS
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

void handleGetLogs() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", g_logBuffer);
}

void handleLogsPage() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  String html = R"rawhtml(
<!DOCTYPE html><html lang='en'><head>
<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>System Logs - HS Sensor</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Inter','Segoe UI',system-ui,sans-serif;background:#030712;color:#94a3b8;min-height:100vh;display:flex;flex-direction:column;padding:24px}
  .container{max-width:900px;margin:0 auto;width:100%;flex:1;display:flex;flex-direction:column}
  header{display:flex;justify-content:space-between;align-items:center;margin-bottom:24px;background:#111827;padding:16px 24px;border-radius:12px;border:1px solid #1f2937;box-shadow:0 4px 6px -1px rgba(0,0,0,.1)}
  h1{font-size:1.25rem;font-weight:700;color:#f8fafc;display:flex;align-items:center;gap:8px}
  .status{font-size:.75rem;padding:4px 10px;border-radius:999px;background:#064e3b;color:#34d399;font-weight:600}
  .log-box{background:#000;border:1px solid #1f2937;border-radius:12px;padding:16px;font-family:'Fira Code','Consolas',monospace;font-size:.85rem;line-height:1.6;color:#10b981;overflow-y:auto;flex:1;box-shadow:inset 0 2px 4px rgba(0,0,0,.5);white-space:pre-wrap;margin-bottom:16px;min-height:400px}
  .controls{display:flex;gap:12px;margin-top:auto}
  .btn{padding:10px 18px;border-radius:8px;font-size?.9rem;font-weight:600;cursor:pointer;transition:all .2s;border:1px solid transparent;display:flex;align-items:center;gap:6px}
  .btn-primary{background:#2563eb;color:#fff}
  .btn-primary:hover{background:#1d4ed8;transform:translateY(-1px)}
  .btn-outline{background:transparent;border-color:#334155;color:#f1f5f9}
  .btn-outline:hover{background:#1e293b;border-color:#475569}
  .footer{margin-top:16px;display:flex;justify-content:space-between;font-size:.75rem;color:#4b5563}
  ::-webkit-scrollbar{width:8px}
  ::-webkit-scrollbar-track{background:#111827}
  ::-webkit-scrollbar-thumb{background:#374151;border-radius:4px}
  ::-webkit-scrollbar-thumb:hover{background:#4b5563}
</style>
</head><body>
<div class='container'>
  <header>
    <h1><span>&#128196;</span> System Logs</h1>
    <div id='status' class='status'>Live</div>
  </header>
  <div id='log-content' class='log-box'>Loading logs...</div>
  <div class='controls'>
    <button class='btn btn-primary' onclick='refresh()'>Refresh Now</button>
    <button class='btn btn-outline' onclick='clearLogs()'>Clear View</button>
    <a href='/wifi' style='text-decoration:none' class='btn btn-outline'>WiFi Settings</a>
    <a href='/get_info' style='text-decoration:none' class='btn btn-outline'>Telemetry API</a>
  </div>
  <div class='footer'>
    <span>ESP32-C3 Sensor Node</span>
    <span id='last-update'>Last update: -</span>
  </div>
</div>
<script>
async function refresh(){
  const s=document.getElementById('status');
  const lc=document.getElementById('log-content');
  const lu=document.getElementById('last-update');
  s.textContent='Updating...';
  try{
    const r=await fetch('/api/logs');
    const t=await r.text();
    lc.textContent=t||'No logs available.';
    lc.scrollTop=lc.scrollHeight;
    lu.textContent='Last update: '+new Date().toLocaleTimeString();
    s.textContent='Live';
    s.style.background='#064e3b';
  }catch(e){
    s.textContent='Error';
    s.style.background='#450a0a';
    console.error(e);
  }
}
function clearLogs(){
  document.getElementById('log-content').textContent='';
}
refresh();
setInterval(refresh,3000);
</script>
</body></html>
)rawhtml";
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Measure raw baseline idle count before WiFi background tasks start
  esp_register_freertos_idle_hook(idle_task_hook);
  delay(1000);
  maxIdleCount = idleCounter;

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000); // 100kHz e mai stabil decat 400kHz
  Wire.setTimeOut(50);    // Anti-freeze: Limit wait to 50ms

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println(F("SSD1306 error!"));
  drawMinimalistLogo();
  delay(2000); // Display logo for 2 seconds

  if (!ina219.begin())
    Serial.println(F("INA219 error!"));
  if (!rtc.begin()) {
    addLog("RTC error!");
  } else {
    // Sync hardware clock with compilation time.
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    DateTime now = rtc.now();
    snprintf(g_timestamp, sizeof(g_timestamp), "%02d:%02d:%02d", now.hour(),
             now.minute(), now.second());
    addLog("RTC synced: " + String(g_timestamp));
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

  // Use WPA2 password to prevent phones from rejecting an Open AP without
  // internet
  if (!wm.autoConnect("HS_Sensor_Setup", "12345678")) {
    Serial.println(F("Failed to connect and hit timeout"));
    delay(3000);
  } else {
    addLog("WiFi connected!");
  }
  // -------------------------
  addLog("WiFi Setup complete");
  esp_wifi_set_ps(WIFI_PS_NONE); // Fortam antena sa stea pornita constant (previne lockup-uri I2C)

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Location", "/logs");
    server.send(302, "text/plain", "Redirecting to logs...");
  });

  espClient.setInsecure(); // Fara verificare certificat pentru eficienta
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setBufferSize(512);

  server.on("/get_info", HTTP_GET, handleGetInfo);
  server.on("/wifi_status", HTTP_GET, handleWifiStatus);
  server.on("/reset_wifi", HTTP_GET, handleResetWiFi);
  server.on("/wifi", HTTP_GET, handleWifiPage);
  server.on("/logs", HTTP_GET, handleLogsPage);
  server.on("/api/logs", HTTP_GET, handleGetLogs);
  server.begin();
  addLog("HTTP server started");

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

  if (g_isBatteryCritical) {
    addLog("CRITICAL BATTERY! Shutting down...");
    updateDisplay(); // Show warning
    delay(5000);     // Wait 5s for user to read
    display.clearDisplay();
    display.display();
    esp_deep_sleep_start();
  }

  server.handleClient();

  // Yield crucial catre task-ul Idle din FreeRTOS pentru a calcula CPU Load
  // eficient si a nu bloca watchdog-ul
  delay(2);
}
