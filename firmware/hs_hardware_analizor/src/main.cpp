#include "logic_analyzer_sump.h"
#include <Arduino.h>

// --- Configurari Hardware (Definite in platformio.ini) ---
// SDA_PIN: 5
// SCL_PIN: 6
// NTC_PIN: 1

void setup() {
  // Initializam analizorul pentru PulseView/SUMP
  // Acesta va folosi UART0 (pe pinii 20/21 ai ESP32-C3)
  logic_analyzer_sump();
}

void loop() {
  // Analizorul ruleaza intr-un task separat FreeRTOS
  delay(1000);
}
