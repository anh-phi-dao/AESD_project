#ifndef NS4168_SPEAKER_H
#define NS4168_SPEAKER_H

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SPK_DOUT_PIN 7  /* DIN / DATA_OUT   */
#define SPK_BCLK_PIN 15 /* CLK / BCLK       */
#define SPK_LRC_PIN  16 /* LRC / LRCLK / WS */

#define AUDIO_SPK_PORT I2S_NUM_1
/* Speaker side */
#define SPK_SAMPLE_RATE     16000                    // Support ws server audio data
#define SPK_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT // Updated to new macro
#define SPK_CHANNELS        1
#define SPK_DMA_BUF_COUNT   8
#define SPK_DMA_BUF_LEN     256 // Optimized for ESP32-S3 with 8MB PSRAM and MAX98357A

typedef struct
{
    i2s_chan_handle_t tx_handle;
    bool              initialized;
    bool              playing;
} ns4168_speaker_context_t;

esp_err_t ns4168_speaker_init(void);
esp_err_t ns4168_speaker_start_playback(void);
esp_err_t ns4168_speaker_stop_playback(void);
esp_err_t ns4168_speaker_write_stereo(const void *buffer,
                                      size_t      buffer_size,
                                      size_t     *bytes_written,
                                      uint32_t    timeout_ms);
esp_err_t ns4168_speaker_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms);
void      ns4168_speaker_adjust_volume(int16_t *buffer, size_t samples, uint8_t volume_percent);
#endif
