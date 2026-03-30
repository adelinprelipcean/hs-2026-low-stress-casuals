#include "soc/gpio_reg.h"
#include "soc/gpio_struct.h"
#include <Arduino.h>

/**
 * Analizor Logic SUMP (Openbench Logic Sniffer) pe ESP32-C3
 *
 * Pini monitorizați:
 * Channel 0: GPIO 5 (SDA)
 * Channel 1: GPIO 6 (SCL)
 * Channel 2: GPIO 7
 * Channel 3: GPIO 8
 *
 * Protocol: Openbench Logic Sniffer (SUMP)
 */

// --- Configurații ---
#define MAX_SAMPLES 64000   // 64 KB de RAM pentru buffer
#define SERIAL_SPEED 115200 // Viteză port serial

// --- Comenzi SUMP ---
#define SUMP_RESET 0x00
#define SUMP_ID 0x02
#define SUMP_RUN 0x01
#define SUMP_SET_DIVIDER 0x80
#define SUMP_SET_READ_DELAY 0x81
#define SUMP_SET_FLAGS 0x82

// --- Variabile Globale ---
uint8_t buffer[MAX_SAMPLES]; // Buffer pentru eșantioane
uint32_t divider = 0; // Divizor frecvență (SUMP standard: 100MHz / (divider+1))
uint32_t readCount = 0;  // Număr de eșantioane de citit
uint32_t delayCount = 0; // Buffer pre-trigger (nefolosit momentan)

/**
 * Funcție pentru trimiterea ID-ului dispozitivului (SLA1)
 * Aceasta informează PulseView că suntem un Logic Sniffer.
 */
void sendID() {
  Serial.write('1');
  Serial.write('A');
  Serial.write('L');
  Serial.write('S');
}

/**
 * Captura propriu-zisă a eșantioanelor.
 * Folosește register-read direct pentru viteză maximă.
 */
void capture() {
  // Calculăm delay-ul bazat pe divider.
  // OBLS are bază 100MHz. ESP32-C3 are 160MHz, ajustăm empiric.
  // Pentru 1MHz, divider-ul PulseView este 99.
  uint32_t totalSamples = readCount;
  if (totalSamples > MAX_SAMPLES)
    totalSamples = MAX_SAMPLES;

  // Dezactivăm întreruperile pentru a menține un timing constant
  noInterrupts();

  for (uint32_t i = 0; i < totalSamples; i++) {
    // Folosim registrul de input pentru performanță
    uint32_t regValue = GPIO.in.val;

    // Extragem starea pinilor 5, 6, 7, 8 (4 biți)
    buffer[i] = (uint8_t)((regValue >> 5) & 0x0F);

    // Timing control (foarte simplificat pentru 1MHz)
    // În funcție de divider, am putea adăuga delay-uri mai complexe aici
    // ets_delay_us(microsecunde) - nu e ideal pentru precizie mare, dar bun
    // pentru start
    if (divider > 0) {
      // Ajustare grosieră a frecvenței
      uint32_t wait = divider / 10; // Experimentat empiric
      for (volatile uint32_t d = 0; d < wait; d++) {
        __asm__ __volatile__("nop");
      }
    }
    // Delay minim pentru a nu depăși bufferul prea repede
    __asm__ __volatile__("nop");
  }

  interrupts();

  // Trimiterea datelor înapoi către PC (ordinea inversă conform protocolului
  // SUMP) PulseView așteaptă datele începând cu ultimul eșantion capturat în
  // mod normal, dar trimitem secvențial pentru simplitate în prima versiune.
  for (int32_t i = totalSamples - 1; i >= 0; i--) {
    Serial.write(buffer[i]);
  }
}

void setup() {
  // Pornim comunicația serială
  Serial.begin(SERIAL_SPEED);

  // Așteptăm stabilizarea USB CDC
  for (int i = 0; i < 10; i++) {
    delay(200);
    Serial.println("DEBUG: Analizor Logic Pornit...");
  }

  // Configurare pini ca intrări cu Pull-up (rezolvă stările nedeterminate)
  pinMode(5, INPUT_PULLUP); // SDA
  pinMode(6, INPUT_PULLUP); // SCL
  pinMode(7, INPUT_PULLUP);
  pinMode(8, INPUT_PULLUP);

  // Buffer curățat la pornire
  memset(buffer, 0, MAX_SAMPLES);
}

/**
     * Mod de monitorizare simplu (textual) pentru Serial Monitor.
     * Afișează starea celor 4 canale sub formă de biți (ex: "0101").
     */
    void
    printSimpleMonitor() {
  uint32_t samplesCount = 100; // Capturăm 100 puncte pentru vizualizare rapidă

  Serial.println("\n--- START MONITORIZARE (GPIO 8 7 6 5) ---");
  noInterrupts();
  for (uint32_t i = 0; i < samplesCount; i++) {
    uint32_t regValue = GPIO.in.val;
    buffer[i] = (uint8_t)((regValue >> 5) & 0x0F);

    // Mică întârziere pentru a nu captura totul instantaneu la 160MHz
    for (volatile int d = 0; d < 1000; d++)
      ;
  }
  interrupts();

  for (uint32_t i = 0; i < samplesCount; i++) {
    // Afișăm biții de la 3 la 0 (GPIO 8 -> 5)
    for (int b = 3; b >= 0; b--) {
      Serial.print((buffer[i] >> b) & 0x01);
    }
    Serial.println();
  }
  Serial.println("--- SFÂRȘIT MONITORIZARE ---\n");
}

void loop() {
  if (Serial.available() > 0) {
    uint8_t cmd = Serial.read();

    switch (cmd) {
    case 'm':
    case 'M':
      printSimpleMonitor();
      break;

    case SUMP_RESET:
      // Resetăm starea de configurare
      divider = 0;
      readCount = 0;
      break;

    case SUMP_ID:
      sendID();
      break;

    case SUMP_RUN:
      capture();
      break;

    case 0x05: // SUMP_METADATA
      // Trimitere metadate către PulseView
      Serial.write((uint8_t)0x01);
      Serial.print("ESP32-C3 Logic Analyzer");
      Serial.write((uint8_t)0x00);
      Serial.write((uint8_t)0x02);
      Serial.print("v0.1");
      Serial.write((uint8_t)0x00);
      Serial.write((uint8_t)0x23);
      Serial.write((uint8_t)0x04);
      Serial.write((uint8_t)0x00); // 4 canale
      Serial.write((uint8_t)0x21); // Buffer size (4 octeți)
      Serial.write((uint8_t)(MAX_SAMPLES & 0xFF));
      Serial.write((uint8_t)((MAX_SAMPLES >> 8) & 0xFF));
      Serial.write((uint8_t)((MAX_SAMPLES >> 16) & 0xFF));
      Serial.write((uint8_t)((MAX_SAMPLES >> 24) & 0xFF));
      Serial.write((uint8_t)0x00);
      Serial.write((uint8_t)0x00); // Terminare
      break;

    case SUMP_SET_DIVIDER:
      // Așteptăm 4 octeți pentru divider (conform protocolului OBLS)
      while (Serial.available() < 4)
        ;
      divider = Serial.read();
      divider |= (Serial.read() << 8);
      divider |= (Serial.read() << 16);
      divider |= (uint32_t)(Serial.read() << 24);
      break;

    case SUMP_SET_READ_DELAY:
      // Așteptăm 4 octeți (citește eșantioane și delay trigger)
      while (Serial.available() < 4)
        ;
      // SUMP trimite counts multiply by 4
      readCount = (Serial.read() | (Serial.read() << 8)) + 1;
      readCount *= 4;
      delayCount = (Serial.read() | (Serial.read() << 8)) + 1;
      delayCount *= 4;
      break;

    case SUMP_SET_FLAGS:
      // Ignorăm flag-urile momentan
      while (Serial.available() < 1)
        Serial.read();
      break;

    default:
      // Altele sunt comenzi complexe sau necunoscute
      break;
    }
  }
}
