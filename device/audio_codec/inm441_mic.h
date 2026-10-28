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
#include "audio_i2s_bus.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

/* Pins, port and sample rate come from the shared I2S bus (see audio_i2s_bus.h). */
#define MIC_WS_PIN     AUDIO_I2S_WS_PIN
#define MIC_SD_PIN     AUDIO_I2S_DIN_PIN
#define MIC_SCK_PIN    AUDIO_I2S_BCLK_PIN
#define AUDIO_MIC_PORT AUDIO_I2S_PORT

/* The INMP441 L/R pin is tied to 3V3 on the board, so its data is in the RIGHT slot (index 1). Use 0 if L/R is
 * changed to GND. */
#define MIC_SLOT_INDEX 1

/* The INMP441 sends 24-bit samples left-justified in a 32-bit word; raw >> MIC_GAIN_SHIFT gives the 16-bit sample
 * (16 = plain top 16 bits, each step below that doubles the digital gain). */
#define MIC_GAIN_SHIFT 14

/* The INMP441 needs a moment after the clock starts before its output settles (DC / filter start-up). These first
 * milliseconds are dropped by inm441_i2s_mic_read(). */
#define MIC_SETTLE_MS 300

/* Audio formats */
#define MIC_SAMPLE_RATE     AUDIO_I2S_SAMPLE_RATE
#define MIC_BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define MIC_CHANNELS        1
#define MIC_DMA_BUF_COUNT   AUDIO_I2S_DMA_DESC
#define MIC_DMA_BUF_LEN     AUDIO_I2S_DMA_FRAMES
#define MIC_READ_LEN        960 /* bytes of mono 16-bit samples per read (30 ms) */

#define MIC_READ_TIMEOUT_MS 150 /* default read timeout when the caller passes 0 */

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
 * @brief Read raw interleaved stereo frames (left, right) from the shared I2S bus.
 *
 * Used to check which slot carries the INMP441 data. Each frame is two raw 32-bit words (24-bit data, left-justified).
 *
 * @param[out] frames       Buffer for interleaved samples.
 * @param[in]  frame_count  Number of stereo frames requested.
 * @param[out] frames_read  Number of stereo frames actually read.
 * @param[in]  timeout_ms   Read timeout in milliseconds, 0 uses the default.
 */
esp_err_t inm441_i2s_mic_read_stereo(int32_t *frames, size_t frame_count, size_t *frames_read, uint32_t timeout_ms);

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
