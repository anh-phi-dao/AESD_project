#include "ns4168_speaker.h"

/***********************************************************************************************************************
 * Global Variables
 **********************************************************************************************************************/

static const char *TAG = "NS4168_SPEAKER";

ns4168_speaker_context_t g_speaker_ctx = {
    .tx_handle   = NULL,
    .initialized = false,
    .playing     = false,
};

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
    ESP_LOGI(TAG, "Initializing speaker I2S channel...");

    // Channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_SPK_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num      = SPK_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num     = SPK_DMA_BUF_LEN;

    // Create TX channel
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, &g_speaker_ctx.tx_handle, NULL), TAG, "Failed to create I2S TX channel");

    // Standard configuration for MAX98357A speaker
    i2s_std_config_t std_cfg
        = {.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SPK_SAMPLE_RATE),
           .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(SPK_BITS_PER_SAMPLE, I2S_SLOT_MODE_STEREO),
           .gpio_cfg = {.mclk         = I2S_GPIO_UNUSED,
                        .bclk         = SPK_BCLK_PIN,
                        .ws           = SPK_LRC_PIN,
                        .dout         = SPK_DOUT_PIN,
                        .din          = I2S_GPIO_UNUSED,
                        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

    esp_err_t ret = i2s_channel_init_std_mode(g_speaker_ctx.tx_handle, &std_cfg);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
        i2s_del_channel(g_speaker_ctx.tx_handle);
        g_speaker_ctx.tx_handle = NULL;
        return ret;
    }
    g_speaker_ctx.initialized = true;
    ESP_LOGI(
        TAG, "Speaker I2S channel initialized (BCLK=%d, LRC=%d, DOUT=%d)", SPK_BCLK_PIN, SPK_LRC_PIN, SPK_DOUT_PIN);
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

    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_speaker_ctx.tx_handle), TAG, "Failed to enable TX channel");
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

    if (g_speaker_ctx.tx_handle)
    {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(g_speaker_ctx.tx_handle), TAG, "Failed to disable TX channel");
    }

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

    *bytes_written = 0;
    esp_err_t ret  = ESP_OK;

    // Giả định mỗi sample là 16-bit (2 bytes)
    const int16_t *in_buf  = (const int16_t *)buffer;
    size_t         samples = buffer_size / sizeof(int16_t);

    // Nếu samples là chẵn → coi như stereo đã interleaved
    if (samples % 2 == 0)
    {
        ret = i2s_channel_write(g_speaker_ctx.tx_handle, buffer, buffer_size, bytes_written, pdMS_TO_TICKS(timeout_ms));
    }
    else
    {
        // Mono: duplicate sang stereo
        static int16_t stereo_buf[512]; // temp buffer, resize theo nhu cầu
        size_t         stereo_samples = samples * 2;
        if (stereo_samples > (sizeof(stereo_buf) / sizeof(int16_t)))
        {
            return ESP_ERR_NO_MEM; // buffer tạm không đủ
        }
        for (size_t i = 0; i < samples; i++)
        {
            stereo_buf[2 * i]     = in_buf[i]; // L
            stereo_buf[2 * i + 1] = in_buf[i]; // R
        }

        ret = i2s_channel_write(g_speaker_ctx.tx_handle,
                                stereo_buf,
                                stereo_samples * sizeof(int16_t),
                                bytes_written,
                                pdMS_TO_TICKS(timeout_ms));
    }

    return ret;
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

    *bytes_written = 0;

    esp_err_t ret
        = i2s_channel_write(g_speaker_ctx.tx_handle, buffer, buffer_size, bytes_written, pdMS_TO_TICKS(timeout_ms));

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
