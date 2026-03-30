# React + Vite ESP32 Telemetry Dashboard

Bun venit la **ESP32 Telemetry Dashboard**! Această aplicație React a fost construită complet de la zero pentru a servi drept monitor în timp real pentru datele hardware transmise direct de un microcontroler ESP32-C3.

## 🚀 Călătoria Proiectului

Povestea acestui proiect a început cu o structură simplă de bază Vite + React. Obiectivul arhitectural inițial a fost construirea unui layout curat, tip terminal în dark-mode, care să conțină module individuale pentru diferite citiri hardware ale ESP32 (Mediu/Senzori, Managementul Energiei, Diagnostice de Sistem și Loguri I/O).

### Faza 1: Structurare și Mocking
Am dezvoltat mai întâi frontend-ul: componente modulare care gestionează totul, de la citirile de temperatură până la tensiunea bateriei și logica semnalului wireless. Pentru a testa interfața fără a avea un ESP32 asamblat la îndemână imediat, am conceput un sistem sofisticat de simulare (mocking) a telemetriei prin intermediul unui hook custom, `useEsp32Telemetry`. Acesta a permis aplicației să simuleze în mod sigur și realist fluctuațiile de rețea, scăderile de tensiune în timp și declanșările aleatorii ale pinilor GPIO.

### Faza 2: De la Local la IoT
Adevărata provocare a apărut când codul efectiv pentru ESP32 a fost configurat. Echipa noastră a optat pentru protocolul MQTT via **HiveMQ Cloud** pentru transmisii IoT rapide.
Am transformat motorul de simulare într-un abonat MQTT activ pe bază de WebSockets. Am importat librăria `mqtt.js`, am stabilit conexiuni securizate WebSocket (WSS pe portul 8884) către HiveMQ și am conectat dashboard-ul la topicul IoT live.

### Faza 3: Perfecționarea Pachetului de Date (Payload-ului)
În ultima etapă, colegii de la hardware au stabilit un format JSON specific pentru telemetrie, incluzând metrici precum `light_intensity`, `current_total`, și `io_log`. Am adaptat strict interfețele noastre TypeScript pentru a respecta exact acest standard de intrare, sincronizând complet dashboard-ul live cu array-urile reale de senzori în mod fluid.

## 🛠️ Stack Tehnologic & Funcționalități
- **Framework & Unelte**: React, TypeScript, Vite
- **Rețelistică**: `mqtt.js` (conectat prin WebSockets folosind wss:// către HiveMQ)
- **UI & Grafice**: Tailwind CSS, Recharts pentru distribuții istorice în timp real sub formă de linii.
- **Senzori Afișați**: Temperatura (°C), Intensitate Luminoasă (Lux), Încărcare CPU, Putere Semnal (RSSI), Tensiune (V), Curent (mA), citiri brute GPIO și Durata estimată a bateriei.

## 🏃‍♂️ Rularea Interfeței Web

Dacă dorești să pornești interfața web pentru a vedea datele de telemetrie primite:

1. Navighează în folderul `web` (sau `esp32-week1`).
2. Instalează dependențele:
   ```bash
   npm install
   ```
3. Pornește serverul de dezvoltare:
   ```bash
   npm run dev
   ```

*(Pentru a schimba configurarea sau host-ul HiveMQ, verifică fișierul `src/hooks/useEsp32Telemetry.ts`)*.
