#include "ns4168_speaker.h"

/***********************************************************************************************************************
 * Global Variables
 **********************************************************************************************************************/

/* i2s_channel_write()/i2s_channel_read() take the timeout in milliseconds, not in FreeRTOS ticks. */
static const char *TAG = "NS4168_SPEAKER";

ns4168_speaker_context_t g_speaker_ctx = {
    .tx_handle   = NULL,
    .initialized = false,
    .playing     = false,
};

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Writes 16-bit PCM to the 32-bit I2S slots of the shared bus (sample << 16). With duplicate set, every input sample is
 * sent to both the left and the right slot (mono input). bytes_written counts input bytes consumed.
 **********************************************************************************************************************/
static esp_err_t ns4168_speaker_write_pcm16(const int16_t *p_samples,
                                            size_t         sample_count,
                                            bool           duplicate,
                                            size_t        *p_bytes_written,
                                            uint32_t       timeout_ms)
{
    int32_t   chunk[SPK_WRITE_CHUNK_SAMPLES * 2U];
    esp_err_t ret = ESP_OK;

    *p_bytes_written = 0;
    for (size_t done = 0; (done < sample_count) && (ESP_OK == ret);)
    {
        size_t count  = 0;
        size_t slots  = 0;

        if (duplicate)
        {
            count = ((sample_count - done) < SPK_WRITE_CHUNK_SAMPLES) ? (sample_count - done) : SPK_WRITE_CHUNK_SAMPLES;
            for (size_t i = 0; i < count; i++)
            {
                chunk[2U * i]        = (int32_t)((uint32_t)(uint16_t)p_samples[done + i] << 16);
                chunk[(2U * i) + 1U] = chunk[2U * i];
            }
            slots = count * 2U;
        }
        else
        {
            count = ((sample_count - done) < (SPK_WRITE_CHUNK_SAMPLES * 2U)) ? (sample_count - done)
                                                                             : (SPK_WRITE_CHUNK_SAMPLES * 2U);
            for (size_t i = 0; i < count; i++)
            {
                chunk[i] = (int32_t)((uint32_t)(uint16_t)p_samples[done + i] << 16);
            }
            slots = count;
        }

        size_t    written = 0;
        ret               = i2s_channel_write(g_speaker_ctx.tx_handle, chunk, slots * sizeof(int32_t), &written, timeout_ms);
        size_t samples_out = written / sizeof(int32_t);
        done += duplicate ? (samples_out / 2U) : samples_out;
        *p_bytes_written = done * sizeof(int16_t);
        if ((ESP_OK == ret) && (0U == samples_out))
        {
            break;
        }
    }
    return ret;
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/**
 * @brief Initialize the NS4168 speaker I2S TX channel.
 *
 * This function configures the I2S TX channel used to transmit audio data
 * to the speaker. The DMA configuration, clock configuration, audio slot
 * configuration, and GPIO pins are initialized according to the configured
 * speaker parameters.
 *
 * @return
 *     - ESP_OK              : Speaker I2S channel initialized successfully.
 *     - ESP_ERR_INVALID_ARG  : Invalid argument was provided to the I2S driver.
 *     - ESP_ERR_NO_MEM       : Not enough memory to allocate the I2S channel
 *                             or associated resources.
 *     - Other esp_err_t      : Error returned by the I2S driver during channel
 *                             creation or initialization.
 */
esp_err_t ns4168_speaker_init(void)
{
    if (g_speaker_ctx.initialized)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(audio_i2s_bus_init(&g_speaker_ctx.tx_handle, NULL), TAG, "Failed to create shared I2S bus");

    const gpio_config_t ctrl_cfg = {
        .pin_bit_mask = 1ULL << SPK_CTRL_PIN,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ctrl_cfg), TAG, "Failed to configure CTRL pin");
    ESP_RETURN_ON_ERROR(gpio_set_level(SPK_CTRL_PIN, !SPK_CTRL_ACTIVE_LEVEL), TAG, "Failed to set CTRL pin");

    g_speaker_ctx.initialized = true;
    ESP_LOGI(TAG, "Speaker ready (CTRL=%d)", SPK_CTRL_PIN);
    return ESP_OK;
}

/**
 * @brief Start speaker playback.
 *
 * Enables the configured I2S TX channel and changes the speaker context
 * state to indicate that playback is active.
 *
 * @return
 *     - ESP_OK             : Playback started successfully, or playback was
 *                            already active.
 *     - ESP_ERR_INVALID_STATE : Speaker I2S channel has not been initialized
 *                               or the TX handle is invalid.
 *     - Other esp_err_t     : Error returned by the I2S driver while enabling
 *                             the TX channel.
 */
esp_err_t ns4168_speaker_start_playback(void)
{
    if (!g_speaker_ctx.initialized || !g_speaker_ctx.tx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_speaker_ctx.playing)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(audio_i2s_bus_start(), TAG, "Failed to start the I2S clock");
    ESP_RETURN_ON_ERROR(gpio_set_level(SPK_CTRL_PIN, SPK_CTRL_ACTIVE_LEVEL), TAG, "Failed to enable amplifier");
    g_speaker_ctx.playing = true;

    ESP_LOGI(TAG, "Playback started");
    return ESP_OK;
}

/**
 * @brief Stop speaker playback.
 *
 * Disables the I2S TX channel when playback is active and updates the
 * speaker context state accordingly.
 *
 * @return
 *     - ESP_OK             : Playback stopped successfully, or playback was
 *                            already stopped.
 *     - Other esp_err_t     : Error returned by the I2S driver while disabling
 *                             the TX channel.
 */
esp_err_t ns4168_speaker_stop_playback(void)
{
    if (!g_speaker_ctx.playing)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(gpio_set_level(SPK_CTRL_PIN, !SPK_CTRL_ACTIVE_LEVEL), TAG, "Failed to disable amplifier");
    /* The TX channel keeps running as the shared clock source; with no data it outputs silence. */

    g_speaker_ctx.playing = false;
    ESP_LOGI(TAG, "Playback stopped");
    return ESP_OK;
}

/**
 * @brief Write audio data to the speaker using stereo output.
 *
 * The input buffer is interpreted as 16-bit audio samples. If the number of
 * input samples is even, the data is treated as interleaved stereo data and
 * written directly to the I2S TX channel. If the number of samples is odd,
 * the input is treated as mono data and each sample is duplicated to both
 * left and right channels.
 *
 * @param[in]  buffer       Pointer to the input audio sample buffer.
 * @param[in]  buffer_size  Size of the input audio buffer in bytes.
 * @param[out] bytes_written Number of bytes written to the I2S TX channel.
 * @param[in]  timeout_ms   Maximum time to wait for the I2S TX operation,
 *                          in milliseconds.
 *
 * @return
 *     - ESP_OK             : Audio data was written successfully.
 *     - ESP_ERR_INVALID_ARG : Invalid buffer, output parameter, or buffer
 *                             size was provided.
 *     - ESP_ERR_INVALID_STATE : Speaker I2S channel has not been initialized
 *                               or the TX handle is invalid.
 *     - ESP_ERR_NO_MEM      : Temporary stereo buffer is not large enough
 *                             for the input mono data.
 *     - ESP_ERR_TIMEOUT     : The I2S write operation timed out.
 *     - Other esp_err_t     : Error returned by the I2S driver during the
 *                             write operation.
 */
esp_err_t ns4168_speaker_write_stereo(const void *buffer,
                                      size_t      buffer_size,
                                      size_t     *bytes_written,
                                      uint32_t    timeout_ms)
{
    if (!buffer || !bytes_written || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_speaker_ctx.initialized || !g_speaker_ctx.tx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* An even sample count is taken as interleaved stereo, an odd one as mono that is sent to both channels. */
    const size_t samples = buffer_size / sizeof(int16_t);

    return ns4168_speaker_write_pcm16((const int16_t *)buffer, samples, (samples % 2U) != 0U, bytes_written, timeout_ms);
}

/**
 * @brief Write audio data directly to the speaker I2S TX channel.
 *
 * The input buffer is passed directly to the I2S driver without performing
 * mono-to-stereo conversion or other audio data processing.
 *
 * @param[in]  buffer        Pointer to the audio data buffer.
 * @param[in]  buffer_size   Size of the audio data buffer in bytes.
 * @param[out] bytes_written Number of bytes successfully written.
 * @param[in]  timeout_ms    Maximum time to wait for the I2S TX operation,
 *                           in milliseconds.
 *
 * @return
 *     - ESP_OK              : Audio data was written successfully.
 *     - ESP_ERR_INVALID_ARG : Invalid buffer, output parameter, or buffer
 *                             size was provided.
 *     - ESP_ERR_INVALID_STATE : Speaker I2S channel has not been initialized
 *                               or the TX handle is invalid.
 *     - ESP_ERR_TIMEOUT     : The I2S write operation timed out.
 *     - Other esp_err_t      : Error returned by the I2S driver during the
 *                             write operation.
 */
esp_err_t ns4168_speaker_write(const void *buffer, size_t buffer_size, size_t *bytes_written, uint32_t timeout_ms)
{
    if (!buffer || !bytes_written || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_speaker_ctx.initialized || !g_speaker_ctx.tx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ns4168_speaker_write_pcm16(
        (const int16_t *)buffer, buffer_size / sizeof(int16_t), false, bytes_written, timeout_ms);

    if (ret != ESP_OK)
    {
        ESP_LOGD(TAG, "I2S write error: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Adjust the volume of 16-bit PCM audio samples.
 *
 * Scales each signed 16-bit PCM sample according to the specified volume
 * percentage. The resulting sample is clamped to the valid signed 16-bit
 * range to prevent overflow.
 *
 * @param[in,out] buffer         Pointer to the 16-bit PCM sample buffer.
 * @param[in]     samples        Number of samples in the buffer.
 * @param[in]     volume_percent Volume level in percent.
 *
 * @return
 *     This function does not return a value.
 */
void ns4168_speaker_adjust_volume(int16_t *buffer, size_t samples, uint8_t volume_percent)
{
    if (!buffer || samples == 0)
    {
        return;
    }

    if (volume_percent == 100)
    {
        return; // No change needed
    }

    for (size_t i = 0; i < samples; i++)
    {
        int32_t sample = (int32_t)buffer[i] * volume_percent / 100;

        // Clamp to prevent overflow
        if (sample > INT16_MAX)
        {
            sample = INT16_MAX;
        }
        else if (sample < INT16_MIN)
        {
            sample = INT16_MIN;
        }

        buffer[i] = (int16_t)sample;
    }
}
