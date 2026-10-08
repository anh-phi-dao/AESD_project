#ifndef MIC_H
#define MIC_H

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

#define INM441_I2S_NUM I2S_NUM_AUTO

/* ---------------- Pin definitions ---------------- */
/* INMP441 microphone (I2S RX) */
#define MIC_WS_PIN  4 /* LRCLK / WS */
#define MIC_SD_PIN  6 /* DATA       */
#define MIC_SCK_PIN 5 /* BCLK       */

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

/*******************************************************************************************************************
 * Typedef definitions
 ******************************************************************************************************************/

struct inm441_i2s_mic_context
{
    i2s_chan_handle_t rx_handle;
    bool              initialized;
    bool              recording;
};

typedef struct inm441_i2s_mic_context inm441_i2s_mic_config_t;

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Initialize the INM441 microphone I2S channel.
 *
 * This function creates and initializes the I2S RX channel using the
 * configured microphone sample rate, DMA buffers, audio format, and GPIOs.
 *
 * @retval ESP_OK                  The microphone I2S channel was initialized successfully.
 * @retval ESP_ERR_INVALID_ARG     The I2S channel configuration is invalid.
 * @retval ESP_ERR_INVALID_STATE   The I2S channel is in an invalid state.
 * @retval ESP_ERR_NO_MEM          Insufficient memory is available for the I2S channel.
 */
esp_err_t inm441_i2s_mic_init(void);

/**
 * @brief Start recording audio from the INM441 microphone.
 *
 * This function enables the I2S RX channel and starts receiving audio data
 * from the microphone.
 *
 * @retval ESP_OK                  Recording started successfully or is already active.
 * @retval ESP_ERR_INVALID_STATE   The microphone I2S channel has not been initialized.
 * @retval ESP_ERR_INVALID_ARG     The I2S channel handle is invalid.
 */
esp_err_t inm441_i2s_mic_start_recording(void);

/**
 * @brief Stop recording audio from the INM441 microphone.
 *
 * This function disables the I2S RX channel and stops receiving audio data
 * from the microphone.
 *
 * @retval ESP_OK                  Recording stopped successfully or is already stopped.
 * @retval ESP_ERR_INVALID_STATE   The I2S channel is in an invalid state.
 * @retval ESP_ERR_INVALID_ARG     The I2S channel handle is invalid.
 */
esp_err_t inm441_i2s_mic_stop_recording(void);

/**
 * @brief Read audio data from the INM441 microphone.
 *
 * This function reads audio data from the I2S RX channel into the specified
 * buffer using the configured read timeout.
 *
 * @param[out] buffer       Pointer to the buffer used to store the audio data.
 * @param[in]  buffer_size  Size of the buffer in bytes.
 * @param[out] bytes_read   Pointer to store the number of bytes actually read.
 * @param[in]  timeout_ms   Read timeout in milliseconds. A value of 0 uses
 *                          the default microphone read timeout.
 *
 * @retval ESP_OK                  Audio data was read successfully.
 * @retval ESP_ERR_INVALID_ARG     The buffer, bytes_read pointer, or buffer size is invalid.
 * @retval ESP_ERR_INVALID_STATE   The microphone I2S channel has not been initialized.
 * @retval ESP_ERR_TIMEOUT         The read operation timed out.
 */
esp_err_t inm441_i2s_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Get the number of bytes available from the microphone.
 *
 * @param[in]  handle           I2S RX channel handle.
 * @param[out] bytes_available  Pointer to store the number of available bytes.
 *
 * @retval ESP_OK                  The available data size was retrieved successfully.
 * @retval ESP_ERR_INVALID_ARG     The bytes_available pointer is NULL.
 * @retval ESP_ERR_INVALID_STATE   The microphone I2S channel is not initialized
 *                                 or recording is not active.
 */
esp_err_t inm441_i2s_mic_get_bytes_available(i2s_chan_handle_t handle, size_t *bytes_available);

/**
 * @brief Read audio data using optimized timeout and buffer handling.
 *
 * This function checks the available audio data before reading and dynamically
 * adjusts the requested read size and timeout to reduce unnecessary blocking.
 *
 * @param[out] buffer       Pointer to the buffer used to store the audio data.
 * @param[in]  buffer_size  Size of the buffer in bytes.
 * @param[out] bytes_read   Pointer to store the number of bytes actually read.
 * @param[in]  timeout_ms   Maximum read timeout in milliseconds.
 *
 * @retval ESP_OK                  Audio data was read successfully.
 * @retval ESP_ERR_INVALID_ARG     The buffer, bytes_read pointer, or buffer size is invalid.
 * @retval ESP_ERR_INVALID_STATE   The microphone is not initialized or recording
 *                                 is not active.
 * @retval ESP_ERR_NOT_FOUND       Not enough data is available for a non-blocking read.
 * @retval ESP_ERR_TIMEOUT         The read operation timed out before receiving data.
 */
esp_err_t inm441_i2s_mic_read_smart(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms);

#endif
