#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// --- Configurari Hardware ---
#define SDA_PIN 5
#define SCL_PIN 6
#define NTC_PIN 2

// Wi-Fi Channel for ESP-NOW (MUST match AP mode of the first ESP32)
#define WIFI_CHANNEL 6

// --- Structura Pachetului ESP-NOW ---
#pragma pack(push, 1)
struct AnalyzerPacket {
  uint8_t header;          // 0xA2
  uint32_t frame_sequence; // Numarul "cadrului" capturat
  uint8_t fragment_id;     // Indexul fragmentului (0 ... total-1)
  uint8_t total_fragments; // Cate fragmente compun acest cadru
  uint16_t analog_val;     // Valoarea ADC a termistorului
  uint16_t samples_count;  // Cate eșantioane/citiri digitale contine data[]
  uint8_t data[230]; // Payload brut. Fiecare byte contine 4 eșantioane de 2
                     // biți (SDA,SCL)
};
#pragma pack(pop)

// --- Variabile Globale ---
// 1 cadru complet ar fi de ex. 10 fragmente * 240 bytes = 2400 bytes
// 1 cadru complet ar fi de ex. 10 fragmente * 230 bytes = 2300 bytes
// Fiecare byte va contine 4 citiri (2 biti per citire: SDA, SCL)
// Deci 2300 bytes * 4 = 9200 citiri (la 1 MHz = 9.2 milisecunde de I2C trafic!)
#define FRAGMENTS_PER_FRAME 10
#define SAMPLES_PER_BYTE 4
#define TOTAL_SAMPLES (FRAGMENTS_PER_FRAME * 230 * SAMPLES_PER_BYTE)

// Buffer temporar in care logam la viteza maxima (1 byte per citire, il
// comprimam mai tarziu)
uint8_t fast_buffer[TOTAL_SAMPLES];

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
esp_now_peer_info_t peerInfo;
uint32_t frame_counter = 0;

void setup_esp_now() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // ESP32 Hack: station channel fix
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Eroare la initializarea ESP-NOW");
    return;
  }

  // Setare peer catre broadcast MAC
  memset(&peerInfo, 0,
         sizeof(peerInfo)); // ZERO OUT struct to avoid garbage ifidx
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Nu s-a putut adauga colegul ESP-NOW");
    return;
  }
  Serial.println("ESP-NOW Initializat pe Canalul 6!");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- BOOT ANALIZOR WIRELESS ---");

  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);

  // Configuratie NTC ADC Pin 2
  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);
  pinMode(NTC_PIN, ANALOG);

  setup_esp_now();
}

/**
 * Captureaza la viteza ultra-mare pinii I2C si ii stocheaza in memoria RAM.
 */
void capture_fast_logic() {
  // Oprim intreruperile pentru acuratete
  noInterrupts();

  for (uint32_t i = 0; i < TOTAL_SAMPLES; i++) {
    // Citire RAW eficienta direct din registru (foarte rapida, MHz range)
    uint32_t regValue = REG_READ(GPIO_IN_REG);

    // Extragem starea GPIO 5 si 6
    // GPIO 5 (SDA) -> bit 0
    // GPIO 6 (SCL) -> bit 1
    uint8_t sda = (regValue >> 5) & 0x01;
    uint8_t scl = (regValue >> 6) & 0x01;

    fast_buffer[i] = (scl << 1) | sda;

    // Mica asteptare hardware pentru a ne apropia the ~1 MHz sampling.
    // Pe ESP32-C3 care ruleaza la 160MHz, un mic delay stabilizeaza fereastra.
    __asm__ __volatile__(
        "nop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop\n\tnop");
  }

  interrupts();
}

/**
 * Functia principala de procesare a fluxului: Capteaza si transmite CADRUL!
 */
void loop() {
  // Interval de capură: 2-3 cadre per secunda pentru a mentine latimea de banda
  // curata O rata prea mare (fara limitare) va gâtui rețeaua.
  static uint32_t last_capture = 0;
  if (millis() - last_capture < 250) {
    delay(1);
    return;
  }
  last_capture = millis();

  // 1. Citim analogicul (o operatiune destul de usoara)
  uint16_t analog_val = analogRead(NTC_PIN);

  // 2. Declanșăm Burst-ul Iologic I2C
  capture_fast_logic();

  // Am capturat cu succes `TOTAL_SAMPLES`. Incrementăm ID-ul cadrului.
  frame_counter++;

  // 3. Comprimam datele de la citit (4 esantioane per byte) si le transmitem
  // prin ESP-NOW
  AnalyzerPacket pkt;
  pkt.header = 0xA2;
  pkt.frame_sequence = frame_counter;
  pkt.total_fragments = FRAGMENTS_PER_FRAME;
  pkt.analog_val = analog_val;

  uint32_t sample_idx = 0;

  for (uint8_t f = 0; f < FRAGMENTS_PER_FRAME; f++) {
    pkt.fragment_id = f;

    // Umplem cele 230 bytes payload cu cele 920 (230 * 4) esantioane I2C
    // asociate
    pkt.samples_count = 230 * SAMPLES_PER_BYTE;

    for (int b = 0; b < 230; b++) {
      uint8_t packed_byte = 0;
      for (int s = 0; s < 4; s++) {
        packed_byte |= (fast_buffer[sample_idx] << (s * 2));
        sample_idx++;
      }
      pkt.data[b] = packed_byte;
    }

    // Trimite fragmentul catre AP
    esp_err_t result =
        esp_now_send(broadcastAddress, (uint8_t *)&pkt, sizeof(AnalyzerPacket));

    if (result != ESP_OK) {
      // Daca a crapat reteaua, pauzam nitel ca sa nu suprasaturam
      delay(2);
    }

    // ESP-NOW cere o mica pauza intre mesaje pentru stabilitate
    delay(5);
  }

  Serial.printf(
      "Cadru #%d trimis. NTC ADC: %d. Dimensiune semnale inramenate: %d\n",
      frame_counter, analog_val, TOTAL_SAMPLES);
}
