#include "ns4168_speaker.h"

static const char *TAG = "NS4168_SPEAKER";

ns4168_speaker_context_t g_speaker_ctx = {
    .tx_handle   = NULL,
    .initialized = false,
    .playing     = false,
};

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
