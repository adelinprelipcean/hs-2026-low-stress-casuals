/* =============================================================================
 * ESP32-C3 Logic Analyzer – Hardware Abstraction Layer
 * =============================================================================
 * Manages DMA buffer allocation, capture lifecycle, and the FreeRTOS task that
 * waits for the DMA-EOF interrupt and delivers samples via callback.
 *
 * Stripped to ESP32-C3 only:
 *   - No PSRAM support (C3 has none)
 *   - No I2S / LCD_CAM paths
 *   - No hi-level interrupt workarounds (Xtensa-only)
 *   - Cache sync removed (not needed on C3 internal SRAM)
 *
 * Original: https://github.com/ok-home/logic_analyzer  (Public Domain / CC0)
 * =============================================================================
 */
#include "logic_analyzer_hal.h"
#include "logic_analyzer_ll.h"
#include "logic_analyzer_hi_level_interrupt.h"
#include "soc/soc.h"
#include "esp_log.h"
#include "string.h"

#define LA_TASK_STACK 2048
#define DMA_FRAME (4096 - 64)  // 3968 bytes per DMA segment

// ---------- frame buffer & DMA descriptors ----------
static la_frame_t la_frame = {
    .fb.buf = NULL,
    .fb.len = 0,
    .dma    = NULL
};

static TaskHandle_t logic_analyzer_task_handle = 0;
static int logic_analyzer_started = 0;

// =============================================================================
// Public: query hardware capabilities at runtime
// =============================================================================
esp_err_t logic_analyzer_get_hw_param(logic_analyzer_hw_param_t *hw)
{
    // ESP32-C3: no PSRAM
    hw->available_psram = 0;
    hw->current_psram   = 0;

    hw->max_channels = LA_HW_MAX_CHANNELS;  // 4
    hw->min_channels = LA_HW_MIN_CHANNELS;  // 4

    if (hw->current_channels > LA_HW_MAX_CHANNELS)
        hw->current_channels = LA_HW_MAX_CHANNELS;
    if (hw->current_channels < LA_HW_MIN_CHANNELS)
        hw->current_channels = LA_HW_MIN_CHANNELS;

    int max_ram = (int)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

    hw->max_sample_rate = LA_HW_MAX_RAM_8_SAMPLE_RATE;  // 80 MHz
    hw->min_sample_rate = LA_HW_MIN_8_SAMPLE_RATE;      // 5 kHz
    hw->max_sample_cnt  = LA_HW_MAX_RAM_8_SAMPLE_CNT;   // 64000 or free RAM
    hw->min_sample_cnt  = LA_HW_MIN_8_SAMPLE_CNT;       // 100

    // Clamp to actual free memory if needed
    if (hw->max_sample_cnt > max_ram)
        hw->max_sample_cnt = max_ram;

    return ESP_OK;
}

// =============================================================================
// DMA descriptor chain builder
// =============================================================================
static dmadesc_t *allocate_dma_descriptors(uint32_t size, uint8_t *buffer)
{
    uint32_t count     = size / DMA_FRAME;
    uint32_t last_size = size % DMA_FRAME;

    dmadesc_t *dma = (dmadesc_t *)heap_caps_calloc(
        (count + 1), sizeof(dmadesc_t), MALLOC_CAP_DMA);
    if (dma == NULL)
        return NULL;

    int x = 0;
    for (; x < count; x++) {
        dma[x].dw0.size    = DMA_FRAME;
        dma[x].dw0.length  = DMA_FRAME;
        dma[x].dw0.suc_eof = 0;
        dma[x].dw0.owner   = 1;
        dma[x].buffer      = buffer + DMA_FRAME * x;
        dma[x].next        = &dma[x + 1];
    }
    // Last descriptor
    dma[x].dw0.size    = last_size;
    dma[x].dw0.length  = last_size;
    dma[x].dw0.suc_eof = 0;
    dma[x].dw0.owner   = 1;
    dma[x].buffer      = buffer + DMA_FRAME * x;
    dma[x].next        = NULL;

    return dma;
}

// =============================================================================
// Stop capture & free resources
// =============================================================================
static void logic_analyzer_stop(void)
{
    logic_analyzer_ll_stop();
    logic_analyzer_ll_deinit_dma_eof_isr();

    if (la_frame.dma) {
        free(la_frame.dma);
        la_frame.dma = NULL;
    }
    if (la_frame.fb.buf) {
        free(la_frame.fb.buf);
        la_frame.fb.buf = NULL;
        la_frame.fb.len = 0;
    }
    logic_analyzer_started = 0;
}

// =============================================================================
// FreeRTOS task: waits for DMA completion then calls user callback
// =============================================================================
static void logic_analyzer_task(void *arg)
{
    logic_analyzer_config_t *cfg = (logic_analyzer_config_t *)arg;

    while (1) {
        int noTimeout = ulTaskNotifyTake(pdFALSE, cfg->meashure_timeout);

        if (noTimeout) {
            // 4 channels → each byte holds 2 nibble-samples
            int l_samples = la_frame.fb.len * 2;
            cfg->logic_analyzer_cb(
                (uint8_t *)la_frame.fb.buf,
                l_samples,
                logic_analyzer_ll_get_sample_rate(cfg->sample_rate),
                cfg->number_channels);
            logic_analyzer_stop();
            vTaskDelete(logic_analyzer_task_handle);
        } else {
            // Timeout — notify caller with NULL buffer
            cfg->logic_analyzer_cb(NULL, 0, 0, 0);
            logic_analyzer_stop();
            vTaskDelete(logic_analyzer_task_handle);
        }
    }
}

// =============================================================================
// Public: start a capture
// =============================================================================
esp_err_t start_logic_analyzer(logic_analyzer_config_t *config)
{
    esp_err_t ret = 0;
    logic_analyzer_hw_param_t hw_param;

    hw_param.current_channels = config->number_channels;
    hw_param.current_psram    = config->samples_to_psram;
    logic_analyzer_get_hw_param(&hw_param);

    // timeout == 0 → stop/reset
    if (config->meashure_timeout == 0) {
        if (logic_analyzer_started) {
            config->logic_analyzer_cb(NULL, 0, 0, 0);
            logic_analyzer_stop();
            vTaskDelete(logic_analyzer_task_handle);
        } else {
            config->logic_analyzer_cb(NULL, 0, 0, 0);
        }
        ret = ESP_OK;
        goto _retcode;
    }

    if (logic_analyzer_started)
        return ESP_ERR_INVALID_STATE;

    logic_analyzer_started = 1;

    if (config->logic_analyzer_cb == NULL)
        goto _ret;
    if (config->number_channels > hw_param.max_channels ||
        config->number_channels < hw_param.min_channels)
        goto _ret;

    for (int i = 0; i < config->number_channels; i++) {
        if (config->pin[i] > LA_HW_MAX_GPIO ||
            config->pin[i] < LA_HW_MIN_GPIO)
            goto _ret;
    }
    if (config->pin_trigger > LA_HW_MAX_GPIO ||
        config->pin_trigger < LA_HW_MIN_GPIO)
        goto _ret;
    if ((config->trigger_edge >= 0 &&
         config->trigger_edge < GPIO_INTR_MAX) == 0)
        goto _ret;
    if (config->sample_rate > hw_param.max_sample_rate ||
        config->sample_rate < hw_param.min_sample_rate)
        goto _ret;
    if (config->number_of_samples > hw_param.max_sample_cnt ||
        config->number_of_samples < hw_param.min_sample_cnt)
        goto _ret;

    // ---- Allocate sample buffer (internal DMA-capable RAM) ----
    {
        uint32_t bytes_to_alloc = config->number_of_samples / 2;  // 4-ch nibble packing
        uint32_t largest_free = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);

        if (largest_free < bytes_to_alloc +
                           ((bytes_to_alloc / DMA_FRAME) + 1) * sizeof(dmadesc_t))
        {
            bytes_to_alloc = largest_free -
                             ((bytes_to_alloc / DMA_FRAME) + 2) * sizeof(dmadesc_t);
        }

        la_frame.fb.len = bytes_to_alloc & ~(DMA_ALIGN - 1);
        la_frame.fb.buf = heap_caps_aligned_alloc(
            DMA_ALIGN, la_frame.fb.len, MALLOC_CAP_DMA);
    }

    if (la_frame.fb.buf == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto _retcode;
    }

    la_frame.dma = allocate_dma_descriptors(la_frame.fb.len, la_frame.fb.buf);
    if (la_frame.dma == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto _freebuf_ret;
    }

    logic_analyzer_ll_config(
        config->pin, config->sample_rate,
        config->number_channels, &la_frame);

    if (pdPASS != xTaskCreate(
            logic_analyzer_task, "la_task",
            LA_TASK_STACK * 4, config,
            uxTaskPriorityGet(NULL),
            &logic_analyzer_task_handle))
    {
        ret = ESP_ERR_NO_MEM;
        goto _freedma_ret;
    }

    ret = logic_analyzer_ll_init_dma_eof_isr(logic_analyzer_task_handle);
    if (ret != ESP_OK)
        goto _freetask_ret;

    // Start capture
    if (config->pin_trigger < 0)
        logic_analyzer_ll_start();
    else
        logic_analyzer_ll_triggered_start(config->pin_trigger, config->trigger_edge);

    return ESP_OK;

_freetask_ret:
    vTaskDelete(logic_analyzer_task_handle);
_freedma_ret:
    free(la_frame.dma);
_freebuf_ret:
    free(la_frame.fb.buf);
_retcode:
    logic_analyzer_started = 0;
    return ret;
_ret:
    logic_analyzer_started = 0;
    return ESP_ERR_INVALID_ARG;
}
