# ESP32 Telemetry Dashboard (Web)

Interfata web pentru monitorizare in timp real a platformei ESP32-C3.

Aplicatia este construita cu React + TypeScript + Vite si primeste date binare prin WebSocket direct de la backend-ul ESP32.

## Ce afiseaza

- IMU 3D (BMI160) cu orientare in timp real (roll/pitch/yaw)
- Temperatura din NTC cu reprezentare grafica
- Energie: tensiune, curent, procent baterie, consum total, timp ramas
- Status module hardware (IMU, RTC, INA219, NTC, WebSocket etc.)
- Harta pinilor ESP32-C3 cu indicatori de stare
- Loguri I/O si evenimente de schimbare de stare

## Protocol date (headers)

Frontend-ul interpreteaza pachete binare pe header:

- `0xA1` - IMU raw stream
- `0xE1` - IMU status/fallback
- `0xD4` - Telemetrie sistem/mediu/energie
- `0xC1` - Status pini + status module

Actualizarea starii este facuta pe tip de pachet (slice updates), astfel pachetele non-IMU nu mai suprascriu orientarea IMU.

## Functionalitati UI recente

- Toggle Light/Dark theme cu persistenta in `localStorage`
- Modul de status pentru hardware (cu bec verde/rosu)
- Pin map ESP32-C3 (buline pe pini + etichete)
- Logare pe tranzitii de stare (pins/modules/stream), cu anti-spam

## Rulare locala

1. Intra in folderul `web`:

```bash
cd web
```

2. Instaleaza dependintele:

```bash
npm install
```

3. Ruleaza in development:

```bash
npm run dev
```

4. Build productie:

```bash
npm run build
```

## Configurare WebSocket

URL-ul WebSocket este definit in:

- `src/hooks/useEsp32Telemetry.ts`

Implicit:

- `ws://192.168.4.1:3333`
