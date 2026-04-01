/**
 * =============================================================================
 *  ESP32-C3 Logic Analyzer  –  SUMP / PulseView Firmware
 * =============================================================================
 *
 *  Board  : ESP32-C3 Super Mini  (or any ESP32-C3-DevKitM-1 compatible)
 *  MCU    : ESP32-C3  (RISC-V, 160 MHz, 320 KB SRAM, no PSRAM)
 *
 *  Overview
 *  --------
 *  Turns the ESP32-C3 into a 3-channel logic analyzer compatible with
 *  PulseView (sigrok) via the SUMP / Openbench Logic Sniffer protocol.
 *  Sampling uses the GPSPI2 peripheral in quad-read mode with GDMA,
 *  allowing up to 80 MHz sample rate with zero CPU load during capture.
 *
 *  Channel Mapping (active channels, directly from platformio.ini):
 *
 *      PulseView CH0  ──>  GPIO 5   (SPI SPID)
 *      PulseView CH1  ──>  GPIO 6   (SPI SPIQ)
 *      PulseView CH2  ──>  GPIO 1   (SPI SPIWP)
 *      CH3–CH7 are unused /disabled (-1)
 *
 *  Communication
 *  -------------
 *  Uses the built-in USB-Serial-JTAG controller (no external UART/FTDI needed).
 *  All ESP-IDF/Arduino logging is disabled to prevent corrupting the binary
 *  SUMP protocol stream over this same USB link.
 *
 *  PulseView Setup
 *  ---------------
 *  1. Flash this firmware and connect the ESP32-C3 via USB.
 *  2. Open PulseView  →  "Connect to Device…"
 *  3. Driver:       Openbench Logic Sniffer & SUMP compatibles (ols)
 *  4. Interface:    Serial Port  →  select the ESP32-C3 COM port
 *  5. Speed:        115200  (or leave default)
 *  6. Click "Scan for devices" → device ESP32 with 8 channels should appear
 *  7. Set sample count & rate, click "Run".
 *     NOTE: You may need to click Run twice with a ~1 s gap on first connect.
 *
 *  Hardware Limits
 *  ---------------
 *   - Max sample rate:  80 MHz  (APB_CLK direct)
 *   - Max sample count: ~60,000 (internal SRAM limited)
 *   - Trigger latency:  ~1.3–1.5 µs (GPIO IRQ, no hi-level interrupt on C3)
 *   - PulseView reports 8 channels; only the lower 4 carry data.
 *
 *  Known PulseView Quirks
 *  ----------------------
 *   - First "Run" after connect may time out – click Run again.
 *   - Trigger mode unreliable with < 1k samples.
 *   - Set trigger pre-fetch to 0% (not implemented).
 *   - PulseView may offer rates above 80 MHz – they will be clamped.
 *
 *  License: Public Domain / CC0
 * =============================================================================
 */

#include <Arduino.h>
#include "esp_log.h"
#include "logic_analyzer_sump.h"

void setup()
{
    // Disable all serial logging – SUMP is a binary protocol over the same USB
    esp_log_level_set("*", ESP_LOG_NONE);

    // Allow USB-Serial-JTAG controller to enumerate on the host
    delay(2000);

    // Launch the SUMP command loop on a dedicated FreeRTOS task
    logic_analyzer_sump();
}

void loop()
{
    // SUMP runs in its own FreeRTOS task; nothing to do here
    delay(100);
}
