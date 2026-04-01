# ESP32-C3 Logic Analyzer  — PulseView / SUMP Firmware

Turns an **ESP32-C3 Super Mini** (or any ESP32-C3-DevKitM-1 compatible) into a
3-channel logic analyzer compatible with **PulseView** (sigrok) via the
**SUMP / Openbench Logic Sniffer** protocol over the built-in USB-Serial-JTAG
interface — no external UART adapter needed.

## Specifications

| Parameter         | Value                                   |
| ----------------- | --------------------------------------- |
| Channels          | 3 active (reported as 8 to PulseView)   |
| Max Sample Rate   | 80 MHz                                  |
| Max Sample Count  | ~60 000 (limited by free internal SRAM) |
| Trigger Latency   | ~1.3–1.5 µs (standard GPIO interrupt)   |
| Communication     | USB-Serial-JTAG (CDC)                   |
| Protocol          | SUMP (Openbench Logic Sniffer)          |

## Channel → GPIO Mapping

| PulseView Channel | ESP32-C3 GPIO | SPI Function |
| :----------------:| :-----------: | :----------: |
| CH0               | GPIO 5        | SPID         |
| CH1               | GPIO 6        | SPIQ         |
| CH2               | GPIO 1        | SPIWP        |
| CH3–CH7           | disabled      | —            |

> To change channel pins, edit the `-D CONFIG_ANALYZER_CHAN_x=` build flags in
> `platformio.ini`.  Only GPIO 0–21 are valid on ESP32-C3.

## How It Works

The firmware uses the **GPSPI2** peripheral in quad-read master mode with the
**GDMA** controller to sample 4 GPIO pins simultaneously at configurable rates.
The SPI peripheral generates the internal clock, and the GDMA writes received
data directly to SRAM without CPU involvement until the DMA-EOF interrupt fires.

A dedicated FreeRTOS task runs the SUMP command loop, listening for commands from
PulseView and replying with captured data.

## Building & Flashing

```bash
# Build
pio run

# Flash (connect ESP32-C3 via USB)
pio run --target upload

# Monitor serial output (not needed during normal analyzer operation)
pio run --target monitor
```

## Connecting PulseView

1. Flash the firmware and connect the ESP32-C3 via USB.
2. Open **PulseView** → **Connect to Device…**
3. **Driver**: `Openbench Logic Sniffer & SUMP compatibles (ols)`
4. **Interface**: `Serial Port` → select the ESP32-C3 COM port
5. **Speed**: `115200`
6. Click **"Scan for devices"** → `ESP32` with 8 channels should appear.
7. Set your desired sample count and sample rate, then click **Run**.

> **Tip:** On the first connection you may need to click **Run twice** with a
> ~1 second gap.  This is a known SUMP/PulseView quirk.

## Known Limitations

- **First Run**: After connecting, the first `Run` command may time out. Click **Run** again.
- **Trigger with < 1k samples**: Trigger mode is unreliable with very small sample counts.
- **Pre-fetch**: Trigger pre-fetch is not implemented — set it to **0%** in PulseView.
- **Sample rate display**: PulseView may offer rates above 80 MHz — they will be clamped internally.
- **Channel count**: PulseView sees 8 channels, but only the lower 3 (CH0–CH2) carry real data.

## Project Structure

```
hs_hardware_analizor/
├── src/
│   └── main.cpp                     # Entry point, SUMP task launcher
├── lib/
│   └── logic_analyzer_c3/           # Self-contained C3-only library
│       ├── library.json             # PlatformIO library descriptor
│       ├── include/                 # Public headers
│       │   ├── logic_analyzer_hal.h
│       │   └── logic_analyzer_sump.h
│       ├── private_include/         # Internal headers
│       │   ├── logic_analyzer_const_definition.h
│       │   ├── logic_analyzer_ll.h
│       │   ├── logic_analyzer_pin_definition.h
│       │   ├── logic_analyzer_serial.h
│       │   └── logic_analyzer_sump_definition.h
│       └── src/                     # Source files
│           ├── logic_analyzer_hal.c   # DMA buffer mgmt, capture lifecycle
│           ├── logic_analyzer_ll.c    # GPSPI2 + GDMA register-level driver
│           └── logic_analyzer_sump.c  # SUMP protocol parser
├── logic_analyzer/                  # Original multi-target library (unused)
└── platformio.ini                   # Build configuration
```

## Credits

Based on [ok-home/logic_analyzer](https://github.com/ok-home/logic_analyzer)
(Public Domain / CC0), stripped and adapted for ESP32-C3 with PlatformIO/Arduino.
