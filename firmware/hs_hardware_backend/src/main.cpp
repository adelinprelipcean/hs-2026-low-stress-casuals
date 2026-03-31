#include <Adafruit_GFX.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DFRobot_BMI160.h>
#include <RTClib.h>
#include <WebSocketsServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <esp_freertos_hooks.h>
#include <algorithm>
#include <vector>

// Packet layout is intentionally compact for low-latency socket streaming.
#pragma pack(push, 1)
struct ImuStreamPacket {
  uint8_t header; // 0xA1 (IMU_PACKET_HEADER_RAW) or 0xE1 (IMU_PACKET_HEADER_STATUS)
  uint32_t sequence;
  uint32_t sampleMicros;
  int16_t gyroX;
  int16_t gyroY;
  int16_t gyroZ;
  int16_t accelX;
  int16_t accelY;
  int16_t accelZ;
};

struct TelemetryPacket {
  uint8_t header; // 0xD4
  uint32_t sampleMs;
  float temp;
  float volt;
  float curr;
  uint8_t bat;
  uint8_t cpu;
  uint8_t rtcHour;
  uint8_t rtcMin;
  uint8_t rtcSec;
};
#pragma pack(pop)

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
#define I2C_SDA_PIN 5
#define I2C_SCL_PIN 6
#define BTN1_PIN 2
#define BTN2_PIN 3
#define TEMP_ADC_PIN 1

// ---------------------------------------------------------
// Peripheral Objects
// ---------------------------------------------------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_INA219 ina219;
RTC_DS3231 rtc;
DFRobot_BMI160 *bmi160 = nullptr;

// WebSocket Server (Port 3333 as requested)
WebSocketsServer webSocket = WebSocketsServer(3333);

#define PCF8591_ADDR 0x48

// ---------------------------------------------------------
// Temperature offset to compensate for component tolerances
#define TEMP_CALIBRATION_OFFSET -10.1f

// Thermistor model for current board thermistor (10k NTC + 10k divider)
#define NTC_NOMINAL_OHMS 10000.0f
#define NTC_BETA_VALUE 3950.0f
#define NTC_FIXED_RESISTOR_OHMS 10000.0f
#define NTC_REFERENCE_TEMP_K (25.0f + 273.15f)
// 1 = NTC is on GND side and fixed resistor goes to 3.3V
// 0 = NTC is on 3.3V side and fixed resistor goes to GND
#define NTC_GND_SIDE_DIVIDER 0

// ---------------------------------------------------------
// Telemetry State
// ---------------------------------------------------------
float g_temperature = -999.0f;
float g_lightLux = 0;
float g_voltage = 0;
float g_current = 0;
float g_avgCurrent = 0; // Stabilizes "Time Left" calculation
float g_totalMah = 0;
bool g_isBatteryCritical = false;
#define BATTERY_CRITICAL_THRESHOLD 3.1f
// Require consecutive low-voltage confirmations before shutdown.
#define BATTERY_CRITICAL_CONFIRM_SAMPLES 5
float g_cpuLoad = 0;
char g_timestamp[32] = "--:--:--";
uint8_t g_displayPage = 0;
#define MAX_PAGES 5

// IMU State
bool g_bmi160Connected = false;
float g_accelX = 0, g_accelY = 0, g_accelZ = 0;
float g_gyroX = 0, g_gyroY = 0, g_gyroZ = 0;

// Raw pin status
uint8_t g_rawAIN0 = 0;
uint16_t g_rawAIN1 = 0;
uint8_t g_rawAIN2 = 0;
uint8_t g_rawAIN3 = 0;
bool g_btn1State = false;
bool g_btn2State = false;

// Power + WiFi health state
bool g_ina219Connected = false;
uint8_t g_lowBatterySampleCount = 0;

// AP settings for phone compatibility
const char *AP_SSID_SECURE = "HS_IMU_STREAM";
const char *AP_PASS_SECURE = "12345678";
const char *AP_SSID_OPEN = "HS_IMU_STREAM_OPEN";
const uint8_t AP_CHANNEL = 6;
const uint8_t AP_MAX_CLIENTS = 4;

// IMU stream settings
const uint8_t IMU_PACKET_HEADER_RAW = 0xA1;
const uint8_t IMU_PACKET_HEADER_STATUS = 0xE1;
const uint8_t TELEMETRY_PACKET_HEADER = 0xD4;
const uint32_t IMU_STREAM_INTERVAL_US = 10000; // 100Hz
uint32_t g_imuPacketSequence = 0;

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
// WebSocket Event Handler
// ---------------------------------------------------------
void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
  case WStype_DISCONNECTED:
    addLog("WS [" + String(num) + "] Disconnected");
    break;
  case WStype_CONNECTED: {
    IPAddress ip = webSocket.remoteIP(num);
    addLog("WS [" + String(num) + "] Connected from " + ip.toString());
    break;
  }
  case WStype_TEXT:
    // Manual command handling if needed
    break;
  case WStype_BIN:
    break;
  case WStype_ERROR:
  case WStype_FRAGMENT_TEXT_START:
  case WStype_FRAGMENT_BIN_START:
  case WStype_FRAGMENT:
  case WStype_FRAGMENT_FIN:
    break;
  }
}

bool startAccessPoint() {
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(120);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.softAPsetHostname("hs-imu-stream");
  WiFi.softAPdisconnect(true);

  IPAddress apIP(192, 168, 4, 1);
  IPAddress apGateway(192, 168, 4, 1);
  IPAddress apSubnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIP, apGateway, apSubnet);

  bool apOk = false;
  for (uint8_t attempt = 0; attempt < 2 && !apOk; attempt++) {
    apOk = WiFi.softAP(AP_SSID_SECURE, AP_PASS_SECURE, AP_CHANNEL, false,
                       AP_MAX_CLIENTS);
    if (!apOk) {
      delay(250);
    }
  }

  if (!apOk) {
    for (uint8_t attempt = 0; attempt < 2 && !apOk; attempt++) {
      apOk =
          WiFi.softAP(AP_SSID_OPEN, nullptr, AP_CHANNEL, false, AP_MAX_CLIENTS);
      if (!apOk) {
        delay(250);
      }
    }
  }

  if (apOk) {
    addLog("AP ready");
    addLog("SSID: " + WiFi.softAPSSID());
    addLog("AP IP: " + WiFi.softAPIP().toString());
    Serial.println("[AP] Started");
    Serial.println("[AP] SSID: " + WiFi.softAPSSID());
    Serial.println("[AP] CH: " + String(AP_CHANNEL));
    Serial.println("[AP] IP: " + WiFi.softAPIP().toString());
  } else {
    addLog("AP start failed");
    Serial.println("[AP] Start FAILED");
  }

  return apOk;
}

// ---------------------------------------------------------
// CPU Load (FreeRTOS Idle Hook)
// ---------------------------------------------------------
volatile uint32_t idleCounter = 0;
float idleCountPerMs = 0.0f;
uint32_t lastCpuSampleMs = 0;
float idleRateHistory[8] = {0};
uint8_t idleRateHistoryIndex = 0;
bool idleRateHistoryFilled = false;

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

  // Daca endTransmission returneaza altceva decat 0, inseamna ca bus-ul e
  // blocat
  if (Wire.endTransmission(true) != 0) {
    Serial.println("I2C Bus Error la PCF8591!");
    return 255;
  }

  delay(5); // Timp pentru conversie ADC

  uint8_t count = Wire.requestFrom((uint8_t)PCF8591_ADDR, (uint8_t)2);
  if (count >= 2) {
    Wire.read();        // Ignoram primul byte (vechi)
    return Wire.read(); // Returnam valoarea actuala
  }
  return 255;
}

// Helper function for Steinhart math
float ntcMath(float resistance) {
  if (resistance < 10.0f || resistance > 1000000.0f)
    return -99.0f;
  float steinhart = logf(resistance / NTC_NOMINAL_OHMS) / NTC_BETA_VALUE +
                    1.0f / NTC_REFERENCE_TEMP_K;
  return 1.0f / steinhart - 273.15f;
}

float ntcResistanceFromAdc(uint16_t adcRaw) {
  if (adcRaw == 0) return 1000000.0f; // Prevent division by zero, return max resistance
  if (adcRaw >= 4095) return 10.0f;    // Prevent saturation issues
#if NTC_GND_SIDE_DIVIDER
  return (NTC_FIXED_RESISTOR_OHMS * (float)adcRaw) / (4095.0f - (float)adcRaw);
#else
  return (NTC_FIXED_RESISTOR_OHMS * (4095.0f - (float)adcRaw)) / (float)adcRaw;
#endif
}

float calculateTemperature(uint16_t adcRaw) {
  // Relaxed range to capture data even at extremes
  if (adcRaw < 1 || adcRaw > 4094) {
    // If it's pure 0 or 4095, it's likely a hardware disconnect
    return -99.0f;
  }

  float resistance = ntcResistanceFromAdc(adcRaw);
  return ntcMath(resistance);
}

// calculateLux removed (LDR disconnected)

// ---------------------------------------------------------
// Sensor Read (called 1Hz)
// ---------------------------------------------------------
void readSensors() {
  uint32_t nowMs = millis();
  uint32_t elapsedMs = nowMs - lastCpuSampleMs;
  lastCpuSampleMs = nowMs;

  if (elapsedMs == 0) {
    return;
  }

  uint32_t curIdle = idleCounter;
  idleCounter = 0;

  float currentIdlePerMs = (float)curIdle / (float)elapsedMs;

  idleRateHistory[idleRateHistoryIndex] = currentIdlePerMs;
  idleRateHistoryIndex = (idleRateHistoryIndex + 1) % 8;
  if (idleRateHistoryIndex == 0) {
    idleRateHistoryFilled = true;
  }

  uint8_t sampleCount = idleRateHistoryFilled ? 8 : idleRateHistoryIndex;
  float peakIdlePerMs = 0.0f;
  for (uint8_t i = 0; i < sampleCount; i++) {
    peakIdlePerMs = std::max(peakIdlePerMs, idleRateHistory[i]);
  }

  if (peakIdlePerMs > 0.01f) {
    idleCountPerMs = peakIdlePerMs;
  } else if (idleCountPerMs < 0.01f) {
    idleCountPerMs = currentIdlePerMs;
  }

  float expectedIdle = idleCountPerMs * (float)elapsedMs;
  float load = (expectedIdle > 1.0f)
                   ? 100.0f * (1.0f - ((float)curIdle / expectedIdle))
                   : 0.0f;

  g_cpuLoad = constrain(load, 0.0f, 100.0f);

  // --- INTERNAL ADC (NTC) ---
  uint16_t samples[100];
  for (int i = 0; i < 100; i++) {
    samples[i] = analogRead(TEMP_ADC_PIN);
    if (i % 25 == 0)
      delay(1);
  }
  std::sort(samples, samples + 100);
  g_rawAIN1 = samples[50];

  float instantTemp = calculateTemperature(g_rawAIN1);

  static uint8_t tempDebugDecimator = 0;
  tempDebugDecimator++;
  if (tempDebugDecimator >= 5) {
    tempDebugDecimator = 0;
    // Calculating raw resistance for debug
    float resDebug = ntcResistanceFromAdc(g_rawAIN1);
    Serial.printf("[TMP] ADC:%d | Res:%.0f | Result:%.1f C\n", g_rawAIN1,
                  resDebug, instantTemp);
  }

  // Fast Startup & Smooth EMA (Alpha = 0.04)
  if (g_temperature < -900.0f) {
    if (instantTemp > -90.0f)
      g_temperature = instantTemp + TEMP_CALIBRATION_OFFSET;
  } else {
    if (instantTemp > -90.0f) {
      float calibrated = instantTemp + TEMP_CALIBRATION_OFFSET;
      g_temperature = (g_temperature * 0.60f) + (calibrated * 0.40f);
    }
  }

  g_lightLux = 0;

  // --- INA219 ---
  delay(5);
  if (g_ina219Connected) {
    g_voltage = ina219.getBusVoltage_V();
    g_current = ina219.getCurrent_mA();
  } else {
    g_voltage = 0.0f;
    g_current = 0.0f;
  }

  // Low-pass EMA Filter (Alpha = 0.05) to stabilize "Time left" estimation
  if (g_avgCurrent == 0)
    g_avgCurrent = g_current;
  else
    g_avgCurrent = (g_avgCurrent * 0.95f) + (g_current * 0.05f);

  g_totalMah += g_avgCurrent * (1.0f / 3600.0f);

  // --- Protection Check ---
  // If voltage is realistically low but above noise (0.5V), trigger shutdown
  if (g_ina219Connected && g_voltage > 2.0f &&
      g_voltage < BATTERY_CRITICAL_THRESHOLD) {
    if (g_lowBatterySampleCount < 250) {
      g_lowBatterySampleCount++;
    }
  } else {
    g_lowBatterySampleCount = 0;
  }
  if (g_lowBatterySampleCount >= BATTERY_CRITICAL_CONFIRM_SAMPLES) {
    g_isBatteryCritical = true;
  }

  // --- RTC ---
  delay(5);
  DateTime now = rtc.now();
  snprintf(g_timestamp, sizeof(g_timestamp), "%02d:%02d:%02d", now.hour(),
           now.minute(), now.second());
}

// ---------------------------------------------------------
// WebSocket Data Stream Logic
// ---------------------------------------------------------

void sendTelemetryToWebSocket() {
  if (webSocket.connectedClients() == 0)
    return;

  DateTime now = rtc.now();
  float batPercent = (g_voltage - 3.0f) / (4.2f - 3.0f) * 100.0f;
  batPercent = constrain(batPercent, 0.0f, 100.0f);

  TelemetryPacket pkt;
  pkt.header = TELEMETRY_PACKET_HEADER;
  pkt.sampleMs = millis();
  pkt.temp = g_temperature;
  pkt.volt = g_voltage;
  pkt.curr = g_current;
  pkt.bat = (uint8_t)batPercent;
  pkt.cpu = (uint8_t)g_cpuLoad;
  pkt.rtcHour = now.hour();
  pkt.rtcMin = now.minute();
  pkt.rtcSec = now.second();

  webSocket.broadcastBIN((uint8_t *)&pkt, sizeof(TelemetryPacket));
}

void streamImuAt100Hz() {
  static uint32_t lastSampleUs = 0;
  uint32_t nowUs = micros();

  if ((uint32_t)(nowUs - lastSampleUs) < IMU_STREAM_INTERVAL_US) {
    return;
  }
  lastSampleUs = nowUs;

  int16_t accelGyro[6] = {0, 0, 0, 0, 0, 0}; // [0..2] = gyro, [3..5] = accel
  bool imuReadOk = false;
  if (g_bmi160Connected && bmi160 != nullptr) {
    int8_t rslt = bmi160->getSensorData(bmi160->bothAccelGyro, accelGyro);
    imuReadOk = (rslt == 0);
  }

  if (imuReadOk) {
    // Update display values regardless of client connection
    g_gyroX = (float)accelGyro[0] / 16.4f;
    g_gyroY = (float)accelGyro[1] / 16.4f;
    g_gyroZ = (float)accelGyro[2] / 16.4f;
    g_accelX = (float)accelGyro[3] / 16384.0f;
    g_accelY = (float)accelGyro[4] / 16384.0f;
    g_accelZ = (float)accelGyro[5] / 16384.0f;
  }

  if (webSocket.connectedClients() == 0) {
    return;
  }

  ImuStreamPacket pkt;
  pkt.header = imuReadOk ? IMU_PACKET_HEADER_RAW : IMU_PACKET_HEADER_STATUS;
  pkt.sequence = g_imuPacketSequence++;
  pkt.sampleMicros = nowUs;
  pkt.gyroX = accelGyro[0];
  pkt.gyroY = accelGyro[1];
  pkt.gyroZ = accelGyro[2];
  pkt.accelX = accelGyro[3];
  pkt.accelY = accelGyro[4];
  pkt.accelZ = accelGyro[5];

  webSocket.broadcastBIN((uint8_t *)&pkt, sizeof(ImuStreamPacket));
}

// ---------------------------------------------------------
// Display Core
// ---------------------------------------------------------
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

    // Centered Temperature Display
    display.setTextSize(1);
    display.setCursor(35, 22);
    display.print("STATION TEMP");

    display.setTextSize(2);
    display.setCursor(30, 38);
    if (g_temperature > -900.0f) {
      display.print(g_temperature, 1);
      display.print((char)247);
      display.print("C");
    } else {
      display.print("---");
    }

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

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    // 2. Timestamp
    display.setCursor(0, 16);
    display.print("TIME:  ");
    display.print(g_timestamp);

    // 3. Raw I/O Data (Fixed horizontal fit)
    display.setCursor(0, 26);
    display.printf("RAW: A0:%03d  A1:%04d", g_rawAIN0, g_rawAIN1);
    display.setCursor(0, 35);
    display.printf("     A2:%03d  A3:%03d", g_rawAIN2, g_rawAIN3);

    // 4. Separator
    display.drawLine(0, 46, 127, 46, SSD1306_WHITE);

    // 5. Processed Data
    display.setCursor(0, 50);
    display.printf("TMP:%2.1fC | LUM:%dlx", g_temperature, (int)g_lightLux);
  } else if (g_displayPage == 3) {
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
  } else if (g_displayPage == 4) {
    drawHeader("IMU / MOTION");
    display.setTextSize(1);

    if (!g_bmi160Connected) {
      display.setCursor(15, 30);
      display.setTextColor(SSD1306_BLACK, SSD1306_WHITE);
      display.print(" SENSOR NOT FOUND ");
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(4, 50);
      display.print("Check ADDR (0x69/68)");
      return;
    }

    // Accel column
    display.setCursor(5, 18);
    display.print("ACC [G]");
    display.setCursor(5, 28);
    display.printf("X: %1.2f", g_accelX);
    display.setCursor(5, 38);
    display.printf("Y: %1.2f", g_accelY);
    display.setCursor(5, 48);
    display.printf("Z: %1.2f", g_accelZ);

    // Gyro column
    display.setCursor(68, 18);
    display.print("GYR [d/s]");
    display.setCursor(68, 28);
    display.printf("X: %d", (int)g_gyroX);
    display.setCursor(68, 38);
    display.printf("Y: %d", (int)g_gyroY);
    display.setCursor(68, 48);
    display.printf("Z: %d", (int)g_gyroZ);
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

bool pollButtons() {
  static bool lastBtn1 = HIGH;
  static bool lastBtn2 = HIGH;
  bool btn1 = digitalRead(BTN1_PIN);
  bool btn2 = digitalRead(BTN2_PIN);

  g_btn1State = (btn1 == LOW);
  g_btn2State = (btn2 == LOW);

  if (btn1 == LOW && lastBtn1 == HIGH) {
    g_displayPage =
        (g_displayPage == 0) ? (MAX_PAGES - 1) : (g_displayPage - 1);
    addLog("Page Prev: " + String(g_displayPage));
  }
  if (btn2 == LOW && lastBtn2 == HIGH) {
    g_displayPage = (g_displayPage + 1) % MAX_PAGES;
    addLog("Page Next: " + String(g_displayPage));
  }

  bool changed =
      (btn1 == LOW && lastBtn1 == HIGH) || (btn2 == LOW && lastBtn2 == HIGH);
  lastBtn1 = btn1;
  lastBtn2 = btn2;
  return changed;
}

void i2c_recovery() {
  pinMode(I2C_SDA_PIN, INPUT_PULLUP);
  pinMode(I2C_SCL_PIN, OUTPUT);
  for (int i = 0; i < 10; i++) {
    digitalWrite(I2C_SCL_PIN, LOW);
    delayMicroseconds(10);
    digitalWrite(I2C_SCL_PIN, HIGH);
    delayMicroseconds(10);
  }
}

void run_i2c_scanner() {
  Serial.println(F("\nI2C Scanner: Scanning addresses 0x01 to 0x7F..."));
  byte error, address;
  int nDevices = 0;
  for (address = 1; address < 127; address++) {
    // Attempting address
    Wire.beginTransmission(address);
    error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("SUCCESS at 0x"));
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
      nDevices++;
    } else if (error == 4) {
      Serial.print(F("Unknown error at 0x"));
      if (address < 16)
        Serial.print("0");
      Serial.println(address, HEX);
    }
    // Small delay between scan attempts
    delay(1);
  }
  if (nDevices == 0)
    Serial.println(F("SCAN FAILED: No I2C devices found"));
  else
    Serial.println(F("Scan complete."));
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n--- SYSTEM BOOT ---"));

  i2c_recovery();

  // Measure raw baseline idle count before background tasks pollute the stats
  esp_register_freertos_idle_hook(idle_task_hook);
  delay(300);
  idleCountPerMs = 0.0f;
  idleCounter = 0;
  lastCpuSampleMs = millis();
  idleRateHistoryIndex = 0;
  idleRateHistoryFilled = false;
  for (uint8_t i = 0; i < 8; i++) {
    idleRateHistory[i] = 0.0f;
  }

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);
  Wire.setTimeOut(50);

  analogSetAttenuation(ADC_11db); // Full range 0V - 3.1V
  analogReadResolution(12);       // 0 - 4095
  pinMode(TEMP_ADC_PIN, ANALOG);

  run_i2c_scanner();

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println(F("SSD1306 error!"));
  drawMinimalistLogo();
  delay(1000);

  g_ina219Connected = ina219.begin();
  if (!g_ina219Connected)
    Serial.println(F("INA219 error!"));

  // --- RTC ---
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

  // --- BMI160 ---
  bmi160 = new DFRobot_BMI160();
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN); // Re-assert pins

  if (bmi160->I2cInit(0x69) == 0) {
    g_bmi160Connected = true;
    addLog("BMI160 OK (0x69)");
  } else {
    // Check 0x68 as fallback (silently, to see if it responds)
    if (bmi160->I2cInit(0x68) == 0) {
      g_bmi160Connected = true;
      addLog("BMI160 OK (0x68 CONFLICT)");
    } else {
      addLog("BMI160 NOT FOUND");
    }
  }

  startAccessPoint();

  // WebSocket Server Setup
  webSocket.begin();
  webSocket.onEvent(onWebSocketEvent);
  addLog("WS Server ready on port 3333");

  delay(1500);
}

void loop() {
  uint32_t now = millis();
  static uint32_t lastSensorMs = 0;
  static uint32_t lastDisplayMs = 0;
  static uint32_t lastApHealthMs = 0;

  // Handle WebSocket clients
  webSocket.loop();

  // High-Speed IMU Stream (100Hz)
  streamImuAt100Hz();

  // Low-Speed Sensors & Telemetry (1Hz)
  if (now - lastSensorMs >= 1000) {
    lastSensorMs = now;
    readSensors();
    sendTelemetryToWebSocket();
  }

  static uint32_t lastButtonMs = 0;
  if (now - lastButtonMs >= 30) {
    lastButtonMs = now;
    if (pollButtons()) {
      updateDisplay();
      lastDisplayMs = now;
    }
  }

  // Display Update (Limited to 5Hz / 200ms)
  if (now - lastDisplayMs >= 200) {
    lastDisplayMs = now;
    updateDisplay();
  }

  if (g_isBatteryCritical) {
    addLog("CRITICAL BATTERY! Shutting down...");
    updateDisplay();
    delay(5000);
    display.clearDisplay();
    display.display();
    esp_deep_sleep_start();
  }

  if (now - lastApHealthMs >= 10000) {
    lastApHealthMs = now;
    bool apHealthy = (WiFi.getMode() == WIFI_AP) &&
                     (WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
    if (!apHealthy) {
      addLog("AP unhealthy, restarting...");
      startAccessPoint();
    }
  }

  // Yield for CPU Load calculation and watchdog safety
  delay(1);
}
