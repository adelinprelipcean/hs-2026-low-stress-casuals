# Low Stress Casuals - Hardware Monitor System

Un sistem IoT avansat, bazat pe microcontrolerul ESP32-C3, destinat monitorizării mediului ambiant, analizei consumului energetic și diagnozei performanțelor de sistem. Acest proiect integrează senzori hardware de precizie, procesare locală a datelor și multiple protocoale de comunicație hibride (MQTT, HTTP, I2C). 

## Arhitectura Sistemului

Sistemul combină capabilități de măsurare locală cu facilități de transmitere a datelor la distanță, expunând starea sa internă prin mai multe interfețe:
- **Interfață Locala:** Display OLED (128x64px), navigație hardware (2 butoane integrate).
- **Interfață Web Locală:** Server HTTP încapsulat pe dispozitiv cu un panou de control dedicat și un REST API de tip JSON.
- **Interfață Cloud:** Client MQTT complet implementat cu raportare telemetrică de securitate ridicată (TLS, port 8883) conectat la brokerul HiveMQ.

De asemenea, sistemul include un modul `WiFiManager` pentru alocarea rețelei la prima pornire (Access Point: `HS_Sensor_Setup`, Parola: `12345678`) fără necesitatea recompilării firmware-ului.

## Caracteristici și Instrumentație

### 1. Monitorizare Mediu
- **Temperatură (°C):** Măsurare prin intermediul unui termistor NTC (100k) integrat în modulul ADC PCF8591, folosind ecuația Steinhart-Hart pentru liniarizare pe planul de referință 25°C.
- **Iluminare (Lux):** Citire liniară a luminii ambientale utilizând o fotorezistență (LDR GL5528) și corecția software a valorilor logaritmice pentru determinarea aproximativă în Lux.

### 2. Analiză Energetică
- **Diagnoză Electrică:** Modul hardware dedicat INA219 montat pe magistrala I2C, care determină simultan tensiunea (V) și curentul (mA).
- **Predicție Timp Baterie:** Software-ul include un filtru EMA (Exponential Moving Average) (Alpha = 0.05) destinat eliminării fluctuațiilor curentului absorbit din cauza comunicării WiFi, rezultând estimarea fiabilă a timpului de descărcare rămas pentru un acumulator tipic de 3200mAh (ex. INR18650MH1).

### 3. Diagnoză Sistem
- **Calcul Încărcare CPU:** Monitorizarea gradului de utilizare a unității centrale (CPU Load %) prin preluarea funcțiilor de tip Idle Hook din sistemul de operare FreeRTOS.
- **RTC (Real-Time Clock):** Sincronizare temporală hardware strictă la nivel de secundă prin modulul I2C DS3231.

## Schema de Conectare (Pinout ESP32-C3)

| Componentă | Pini Alocați | Descriere |
| :--- | :--- | :--- |
| **Magistrala I2C (SDA / SCL)** | `GPIO 5` / `GPIO 6` | Viteza bus-ului la 100KHz. Comun pentru PCF8591 (ADC), INA219 (Sursă), DS3231 (RTC), OLED SSD1306. |
| **Buton Control 1** | `GPIO 2` | Avansează spre pagina următoare. Configurat cu rezistor de pull-up intern activat. |
| **Buton Control 2** | `GPIO 3` | Revine la pagina anterioară. Configurat cu rezistor de pull-up intern activat. |

> **Observație Hardware:** Canalele Analog/Digital (ADC) pentru modulul PCF8591 sunt configurate pe adresa fizică prestabilită `0x48`: AIN0 deservește fotorezistența, iar AIN1 deservește termistorul NTC.

## Soluții Tehnice Implementate

- **Toleranța Repeated-Start ESP32:** S-a utilizat un mecanism de re-declanșare la nivel de bibliotecă `Wire.endTransmission(true)` pentru stabilitatea protocolului I2C față de PCF8591 - specifică citirii mai multor canale analogice independent.
- **Prevenire Cadere Tensiune (Brown-out):** Modulul radio a fost inițializat cu instrucțiunea `WiFi.setTxPower(WIFI_POWER_8_5dBm)` pentru a preveni instabilitatea alimentării microcontrolerului la prima generare a semnalului de radio-frecvență.

## Configurare Mediu și Compilare (PlatformIO)

Proiectul este structurat sub standardul PlatformIO.

1. Se va clona depozitul de resurse local și se va deschide sub Visual Studio Code utilând extensia [PlatformIO IDE](https://platformio.org/).
2. Bibliotecile impuse (precum `Adafruit INA219`, `Adafruit SSD1306`, `RTClib`, `WiFiManager`, `PubSubClient`) sunt indicate de fișierul `platformio.ini` și se vor importa automat de utilitar în momentul compilării (sau printr-o directivă de rebuild integrată).
3. Se dispune compilarea și instalarea sistemului (`pio run -t upload`), asigurând conexiunea USB cu mediul compatibil ESP32-C3 Dev Module (`esp32-c3-devkitm-1`). Baud-rate default: `115200`.

## Format JSON (Structură Date Telemetrice API)

Accesul structurat prin port 80 pe URI `/api/data` întoarce un format unificat, inclusiv matricea de stare a interfeței I/O:

```json
{
  "time": "14:48:08",
  "cpu_load_pct": 14.5,
  "wifi_rssi_dbm": -55,
  "pcf8591_ain0_raw": 180,
  "pcf8591_ain1_raw": 120,
  "sensor_light_lux": 420.5,
  "sensor_temperature_c": 23.4,
  "ina219_voltage_v": 3.900,
  "ina219_current_ma": 85.0,
  "digital_btn1_pressed": false,
  "digital_btn2_pressed": false,
  "io_ports": { ... }
}
```
