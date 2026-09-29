#ifndef MIC_H
#define MIC_H
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define INM441_I2S_NUM I2S_NUM_AUTO

/* ---------------- Pin definitions ---------------- */
/* INMP441 microphone (I2S RX) */
#define MIC_WS_PIN  40 /* LRCLK / WS */
#define MIC_SD_PIN  17 /* DATA       */
#define MIC_SCK_PIN 39 /* BCLK       */

#define AUDIO_MIC_PORT I2S_NUM_0

/* ---------------- Audio formats ------------------- */
/* Microphone side */
#define MIC_SAMPLE_RATE     16000
#define MIC_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT // Updated to new macro
#define MIC_CHANNELS        1
#define MIC_DMA_BUF_COUNT   16  /* 16 buffers * 480 bytes/buf * 2 B/sample = 15.36 KB total DMA */
#define MIC_DMA_BUF_LEN     480 /* Aligned with read size (2 * 480 = 960) */
#define MIC_READ_LEN        960 /* Reduced: 1024 samples = 64ms of audio (responsive streaming) */

/* Continuous I2S configuration */
#define MIC_TOTAL_DMA_SIZE  (MIC_DMA_BUF_COUNT * MIC_DMA_BUF_LEN * 2) /* Recalculated total DMA buffer */
#define MIC_READ_TIMEOUT_MS 150                                       /* Configurable read timeout (was hardcoded) */
#define MIC_MIN_DATA_READY  MIC_READ_LEN                              /* Minimum bytes needed before attempting read */

/* Recording size helper (bytes) */
#define RECORD_SECONDS    5
#define FLASH_RECORD_SIZE (MIC_CHANNELS * MIC_SAMPLE_RATE * (MIC_BITS_PER_SAMPLE / 8) * RECORD_SECONDS)

/*Streaming player */
#define STREAM_QUEUE_LEN  50  // Increased from 30 to 50 for better buffering
#define CHUNK_MAX_BYTES   960 // Match MIC_READ_LEN
#define WAIT_FOR_CHUNK_MS 15  // Reduce timeout

struct inm441_i2s_mic_context
{
    i2s_chan_handle_t rx_handle;
    bool              initialized;
    bool              recording;
};
typedef struct inm441_i2s_mic_context inm441_i2s_mic_config_t;

esp_err_t inm441_i2s_mic_init(void);
esp_err_t inm441_i2s_mic_start_recording(void);
esp_err_t inm441_i2s_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);
esp_err_t inm441_i2s_mic_get_bytes_available(i2s_chan_handle_t handle, size_t *bytes_available);
esp_err_t inm441_i2s_mic_read_smart(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);
#endif
