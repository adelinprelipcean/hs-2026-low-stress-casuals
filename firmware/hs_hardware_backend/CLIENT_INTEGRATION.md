# Ghid de Integrare - Sistem de Monitorizare HS (ESP32)

Acest document descrie protocolul de comunicare prin WebSockets pentru integrarea cu aplicații Web și Mobile.

## Detalii Conexiune
- **Protocol**: WebSocket (ws://)
- **Adresă AP**: `192.168.4.1`
- **Port**: `3333`
- **SSID**: `HS_IMU_STREAM` (Parolă: `12345678`)

---

## 1. Flux Telemetrie (1Hz) - Binary (Header 0xD4)
Acest flux este acum binar pentru eficiență maximă.

**Structură Pachet (22 bytes):**
| Offset | Dimensiune | Tip | Descriere |
| :--- | :--- | :--- | :--- |
| 0 | 1 byte | uint8 | **Header**: `0xD4` |
| 1 | 4 bytes | uint32 | **SampleMs**: Timestamp local (millis) |
| 5 | 4 bytes | float | **Temperature** (Celsius) |
| 9 | 4 bytes | float | **Voltage** (V) |
| 13 | 4 bytes | float | **Current** (mA) |
| 17 | 1 byte | uint8 | **Battery %** (0-100) |
| 18 | 1 byte | uint8 | **CPU Load %** (0-100) |
| 19 | 1 byte | uint8 | **RTC Hour** |
| 20 | 1 byte | uint8 | **RTC Minute** |
| 21 | 1 byte | uint8 | **RTC Second** |

---

## 2. Flux IMU High-Speed (100Hz) - Binary (Header 0xA1)
Acest flux rămâne binar, optimizat pentru latență minimă.
Acest flux este optimizat pentru latență minimă și include datele brute de la accelerometru și giroscop.

**Structură Pachet (21 bytes):**
| Offset | Dimensiune | Tip | Descriere |
| :--- | :--- | :--- | :--- |
| 0 | 1 byte | uint8 | **Header**: `0xA1` (Raw) sau `0xE1` (Status) |
| 1 | 4 bytes | uint32 | **Sequence**: ID auto-incremental pentru detectarea pierderii de pachete |
| 5 | 4 bytes | uint32 | **SampleMicros**: Timestamp-ul local al ESP32 în microsecunde |
| 9 | 2 bytes | int16 | **Gyro X** (Scară: ±2000dps) |
| 11 | 2 bytes | int16 | **Gyro Y** |
| 13 | 2 bytes | int16 | **Gyro Z** |
| 15 | 2 bytes | int16 | **Accel X** (Scară: ±2g) |
| 17 | 2 bytes | int16 | **Accel Y** |
| 19 | 2 bytes | int16 | **Accel Z** |

*Notă: Toate valorile multi-byte sunt în format **Little-Endian**.*

---

## 3. Exemple Implementare Client

### Web (JavaScript)
```javascript
const socket = new WebSocket('ws://192.168.4.1:3333');
socket.binaryType = 'arraybuffer';

socket.onmessage = (event) => {
  const dv = new DataView(event.data);
  const header = dv.getUint8(0);

  if (header === 0xD4) {
    // Procesare Telemetrie (1Hz)
    const temp = dv.getFloat32(5, true);
    const volt = dv.getFloat32(9, true);
    console.log('Telemetrie:', { temp, volt });
  } else if (header === 0xA1) {
    // Procesare IMU (100Hz)
    const sequence = dv.getUint32(1, true);
    const gyroX = dv.getInt16(9, true);
    // ... restul valorilor
  }
};
```

### Android (Kotlin - OkHttp)
```kotlin
override fun onMessage(webSocket: WebSocket, bytes: ByteString) {
    val buffer = ByteBuffer.wrap(bytes.toByteArray()).order(ByteOrder.LITTLE_ENDIAN)
    val header = buffer.get().toInt() and 0xFF
    if (header == 0xA1) {
        val sequence = buffer.getInt()
        val sampleMicros = buffer.getInt()
        val gyroX = buffer.getShort()
        // ... procesare IMU High-Speed
    }
}
```

---

## 4. Recomandări Performanță
1. **Throttling UI**: Chiar dacă datele vin la 100Hz, actualizați UI-ul (graficele) la maxim 60 FPS pentru a nu supraîncărca dispozitivul mobil/browser-ul.
2. **Ping/Pong**: WebSocket-ul de pe ESP32 gestionează automat timeout-ul, dar clientul trebuie să poată face auto-reconnect dacă semnalul AP-ului scade.
