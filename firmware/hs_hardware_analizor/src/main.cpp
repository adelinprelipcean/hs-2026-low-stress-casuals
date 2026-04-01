#include "logic_analyzer_sump.h"
#include <Arduino.h>
#include "esp_log.h"

// Declararea externa a generatorului de semnale de test (din logic_analyzer_test_sample.c)
extern "C" void test_sample_init();

void setup() {
  // Dezactivam complet logurile seriale pentru a nu corupe protocolul SUMP
  esp_log_level_set("*", ESP_LOG_NONE);

  // Initializam generatorul de semnale de test
  // Acesta va genera semnale pe diversi pini (4, 5, 6, 7) pentru a verifica analizorul
  test_sample_init();

  // Initializam analizorul pentru PulseView/SUMP
  // Acesta va folosi USB-Serial-JTAG (pinii interni 18/19 pe Super Mini)
  logic_analyzer_sump();
}

void loop() {
  // Analizorul ruleaza intr-un task separat FreeRTOS
  delay(1000);
}
