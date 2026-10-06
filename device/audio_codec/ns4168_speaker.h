#ifndef NS4168_SPEAKER_H
#define NS4168_SPEAKER_H

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

/*******************************************************************************************************************
 * Typedef definitions
 ******************************************************************************************************************/

/**
 * @brief NS4168 speaker I2S context.
 *
 * This structure stores the I2S TX channel handle and the current state
 * of the speaker I2S interface.
 */
typedef struct
{
    i2s_chan_handle_t tx_handle;
    bool              initialized;
    bool              playing;
} ns4168_speaker_context_t;

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Initialize the NS4168 speaker I2S interface.
 *
 * This function creates and initializes the I2S TX channel using the
 * configured speaker parameters, including sample rate, sample width,
 * DMA configuration, and GPIO configuration.
 *
 * @return
 *     - ESP_OK               : Speaker I2S interface initialized successfully.
 *     - ESP_ERR_INVALID_ARG  : Invalid argument was provided to the I2S driver.
 *     - ESP_ERR_NO_MEM       : Not enough memory to allocate the I2S channel
 *                              or associated resources.
 *     - Other esp_err_t      : Error returned by the I2S driver during
 *                              channel creation or initialization.
 */
esp_err_t ns4168_speaker_init(void);

/**
 * @brief Start speaker playback.
 *
 * Enables the I2S TX channel and updates the speaker context to indicate
 * that playback is active.
 *
 * @return
 *     - ESP_OK                  : Playback started successfully, or playback
 *                                 was already active.
 *     - ESP_ERR_INVALID_STATE   : Speaker I2S interface has not been
 *                                 initialized or the TX handle is invalid.
 *     - Other esp_err_t         : Error returned by the I2S driver while
 *                                 enabling the TX channel.
 */
esp_err_t ns4168_speaker_start_playback(void);

/**
 * @brief Stop speaker playback.
 *
 * Disables the I2S TX channel when playback is active and updates the
 * speaker context to indicate that playback has stopped.
 *
 * @return
 *     - ESP_OK          : Playback stopped successfully, or playback was
 *                         already stopped.
 *     - Other esp_err_t : Error returned by the I2S driver while disabling
 *                         the TX channel.
 */
esp_err_t ns4168_speaker_stop_playback(void);

/**
 * @brief Write audio data to the speaker with stereo handling.
 *
 * The input data is interpreted as 16-bit audio samples. Stereo interleaved
 * data is transmitted directly, while mono data is duplicated to both the
 * left and right channels before transmission.
 *
 * @param[in]  buffer        Pointer to the input audio data buffer.
 * @param[in]  buffer_size   Size of the input audio data buffer in bytes.
 * @param[out] bytes_written Number of bytes written to the I2S TX channel.
 * @param[in]  timeout_ms    Maximum time to wait for the I2S write operation,
 *                           in milliseconds.
 *
 * @return
 *     - ESP_OK                : Audio data was written successfully.
 *     - ESP_ERR_INVALID_ARG   : Invalid buffer, output parameter, or buffer
 *                               size was provided.
 *     - ESP_ERR_INVALID_STATE : Speaker I2S interface has not been
 *                               initialized or the TX handle is invalid.
 *     - ESP_ERR_NO_MEM        : Temporary buffer is not large enough to
 *                               convert the input mono data to stereo.
 *     - ESP_ERR_TIMEOUT       : The I2S write operation timed out.
 *     - Other esp_err_t       : Error returned by the I2S driver during
 *                               the write operation.
 */
esp_err_t ns4168_speaker_write_stereo(const void *buffer,
                                      size_t      buffer_size,
                                      size_t     *bytes_written,
                                      uint32_t    timeout_ms);

/**
 * @brief Write audio data directly to the speaker I2S TX channel.
 *
 * The input audio buffer is passed directly to the I2S driver without
 * performing mono-to-stereo conversion or other audio data processing.
 *
 * @param[in]  buffer        Pointer to the input audio data buffer.
 * @param[in]  buffer_size   Size of the input audio data buffer in bytes.
 * @param[out] bytes_written Number of bytes successfully written.
 * @param[in]  timeout_ms    Maximum time to wait for the I2S write operation,
 *                           in milliseconds.
 *
 * @return
 *     - ESP_OK                : Audio data was written successfully.
 *     - ESP_ERR_INVALID_ARG   : Invalid buffer, output parameter, or buffer
 *                               size was provided.
 *     - ESP_ERR_INVALID_STATE : Speaker I2S interface has not been
 *                               initialized or the TX handle is invalid.
 *     - ESP_ERR_TIMEOUT       : The I2S write operation timed out.
 *     - Other esp_err_t       : Error returned by the I2S driver during
 *                               the write operation.
 */
esp_err_t ns4168_speaker_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Adjust the volume of 16-bit PCM audio samples.
 *
 * Scales each audio sample according to the specified volume percentage.
 * The resulting sample is clamped to the valid signed 16-bit range to
 * prevent overflow.
 *
 * @param[in,out] buffer         Pointer to the 16-bit PCM audio sample buffer.
 * @param[in]     samples        Number of samples in the buffer.
 * @param[in]     volume_percent Volume level in percent.
 *
 * @return
 *     This function does not return a value.
 */
void ns4168_speaker_adjust_volume(int16_t *buffer, size_t samples, uint8_t volume_percent);

#endif
