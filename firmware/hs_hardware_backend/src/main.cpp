#include <Adafruit_GFX.h>
#include <Adafruit_INA219.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

extern "C" {
#include "esp_freertos_hooks.h"
}

// ---------------------------------------------------------
// Configuration
// ---------------------------------------------------------
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
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
float g_lightPercent = 0;
float g_voltage = 0;
float g_current = 0;
float g_totalMah = 0;
float g_cpuLoad = 0;
char g_timestamp[32] = "--:--:--";
uint8_t g_displayPage = 0;
#define MAX_PAGES 3

// ---------------------------------------------------------
// CPU Load (FreeRTOS Idle Hook)
// ---------------------------------------------------------
volatile uint32_t idleCounter = 0;
uint32_t maxIdleCount = 0;

bool idle_task_hook() {
  idleCounter++;
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
  float load = (maxIdleCount > 0)
                   ? 100.0f * (1.0f - ((float)curIdle / maxIdleCount))
                   : 0.0f;
  g_cpuLoad = constrain(load, 0.0f, 100.0f);

  // --- PCF8591 ---
  delay(10);
  uint8_t rawLDR = readPCF8591(0); // AIN0 = Fotorezistenta (LDR)
  delay(2);
  uint8_t rawNTC = readPCF8591(1); // AIN1 = Termistorul NTC! (0x41)

  // Calibrare empirica pentru hardware-ul tau:
  int mappedLight = map(rawLDR, 90, 230, 100, 0);
  g_lightPercent = constrain(mappedLight, 0, 100);

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
  g_totalMah += g_current * (1.0f / 3600.0f);

  // --- RTC ---
  delay(5);
  DateTime now = rtc.now();
  snprintf(g_timestamp, sizeof(g_timestamp), "%02d:%02d:%02d", now.hour(),
           now.minute(), now.second());
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
    display.print((int)g_lightPercent);
    display.setTextSize(1);
    display.print(" %");

  } else if (g_displayPage == 1) {
    drawHeader("POWER SYSTEMS");
    display.setTextSize(1);
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

    display.drawLine(5, 34, 123, 34, SSD1306_WHITE);

    display.setCursor(22, 40);
    display.print("Used Capacity");
    display.setTextSize(2);
    display.setCursor(
        max(0, 64 - (int)(String(g_totalMah, 1).length() * 12) / 2), 50);
    display.print(g_totalMah, 1);
    display.setTextSize(1);

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
  }

  for (uint8_t i = 0; i < MAX_PAGES; i++) {
    if (i == g_displayPage)
      display.fillCircle(54 + (i * 10), 61, 2, SSD1306_WHITE);
    else
      display.drawCircle(54 + (i * 10), 61, 2, SSD1306_WHITE);
  }
  display.display();
}

void pollButtons() {
  static bool lastBtn1 = HIGH;
  static bool lastBtn2 = HIGH;
  bool btn1 = digitalRead(BTN1_PIN);
  bool btn2 = digitalRead(BTN2_PIN);
  if (btn1 == LOW && lastBtn1 == HIGH)
    g_displayPage = (g_displayPage + 1) % MAX_PAGES;
  if (btn2 == LOW && lastBtn2 == HIGH)
    g_displayPage =
        (g_displayPage == 0) ? (MAX_PAGES - 1) : (g_displayPage - 1);
  lastBtn1 = btn1;
  lastBtn2 = btn2;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  pinMode(BTN1_PIN, INPUT_PULLUP);
  pinMode(BTN2_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
    Serial.println(F("SSD1306 error!"));
  drawMinimalistLogo();

  if (!ina219.begin())
    Serial.println(F("INA219 error!"));
  if (!rtc.begin())
    Serial.println(F("RTC error!"));

  esp_register_freertos_idle_hook(idle_task_hook);
  delay(1000);
  maxIdleCount = idleCounter;

  delay(1500);
}

void loop() {
  uint32_t now = millis();
  static uint32_t lastSensorMs = 0;
  static uint32_t lastDisplayMs = 0;

  if (now - lastSensorMs >= 1000) {
    lastSensorMs = now;
    readSensors();
  }

  if (now - lastDisplayMs >= 100) {
    lastDisplayMs = now;
    pollButtons();
    updateDisplay();
  }
}
