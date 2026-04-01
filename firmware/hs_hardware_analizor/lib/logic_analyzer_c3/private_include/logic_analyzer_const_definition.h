#pragma once

// =============================================================================
// ESP32-C3 Logic Analyzer – Hardware Constants
// =============================================================================
// Defines chip-level constants for the ESP32-C3 GPSPI2+GDMA sampling engine.
// ESP32-C3 has 4 data channels via SPI Quad-Read mode (SPID, SPIQ, SPIWP, SPIHD).
//
// NOTE: ESP32-C3 does NOT have I2S or LCD_CAM peripherals used by other targets.
//       Sampling is done via the GPSPI2 peripheral in master receive-only mode.
// =============================================================================

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include <driver/gpio.h>
#include "hal/gpio_types.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"

#include "driver/spi_master.h"

#include "esp_idf_version.h"

// ESP32-C3: no hi-level interrupt support, no PSRAM
#undef LA_HW_PSRAM
#undef CONFIG_ANALYZER_USE_HI_LEVEL_INTERRUPT

// Clock source: APB_CLK = 80 MHz, SPI divider range gives effective rates
#define LA_HW_CLK_SAMPLE_RATE 160000000

// GPIO range for ESP32-C3: GPIO0..GPIO21 (-1 = disabled)
#define LA_HW_MIN_GPIO -1
#define LA_HW_MAX_GPIO 21

// DMA descriptor alignment
#define DMA_ALIGN 32

// Sampling limits
#define LA_MAX_SAMPLE_RATE 80000000    // 80 MHz max (APB_CLK / 1)
#define LA_MIN_SAMPLE_RATE 5000        // 5 kHz min
#define LA_MAX_SAMPLE_CNT  64000       // limited by internal SRAM
#define LA_MIN_SAMPLE_CNT  100

// ESP32-C3 always uses exactly 4 channels (SPI Quad mode)
#define LA_HW_MAX_CHANNELS 4
#define LA_HW_MIN_CHANNELS 4

// Rate/count bounds (uniform across 8/16 column for compatibility with HAL API)
#define LA_HW_MIN_8_SAMPLE_RATE    LA_MIN_SAMPLE_RATE
#define LA_HW_MIN_8_SAMPLE_CNT    LA_MIN_SAMPLE_CNT
#define LA_HW_MIN_16_SAMPLE_RATE   LA_MIN_SAMPLE_RATE
#define LA_HW_MIN_16_SAMPLE_CNT   LA_MIN_SAMPLE_CNT

#define LA_HW_MAX_PSRAM_8_SAMPLE_RATE  LA_MAX_SAMPLE_RATE
#define LA_HW_MAX_PSRAM_16_SAMPLE_RATE LA_MAX_SAMPLE_RATE
#define LA_HW_MAX_RAM_8_SAMPLE_RATE    LA_MAX_SAMPLE_RATE
#define LA_HW_MAX_RAM_16_SAMPLE_RATE   LA_MAX_SAMPLE_RATE

#define LA_HW_MAX_PSRAM_8_SAMPLE_CNT   LA_MAX_SAMPLE_CNT
#define LA_HW_MAX_PSRAM_16_SAMPLE_CNT  LA_MAX_SAMPLE_CNT
#define LA_HW_MAX_RAM_8_SAMPLE_CNT     LA_MAX_SAMPLE_CNT
#define LA_HW_MAX_RAM_16_SAMPLE_CNT    LA_MAX_SAMPLE_CNT
