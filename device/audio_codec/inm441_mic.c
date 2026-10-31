#include "inm441_mic.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

#define MIC_CHUNK_FRAMES 128U /* stereo frames converted per I2S read, keeps the stack buffer small */

/*******************************************************************************************************************
 * Global Variables
 ******************************************************************************************************************/

/* i2s_channel_read() takes the timeout in milliseconds, not in FreeRTOS ticks. */
static const char *TAG = "INM441_MIC";

static size_t s_settle_frames = 0; /* frames still to drop after the microphone was started */

inm441_i2s_mic_config_t g_mic_ctx = {
    .rx_handle   = NULL,
    .initialized = false,
    .recording   = false,
};

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

esp_err_t inm441_i2s_mic_init(void)
{
    if (g_mic_ctx.initialized)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(audio_i2s_bus_init(NULL, &g_mic_ctx.rx_handle), TAG, "Failed to create shared I2S bus");

    g_mic_ctx.initialized = true;
    ESP_LOGI(TAG, "Microphone ready (%d Hz, mono from %s slot)", MIC_SAMPLE_RATE, MIC_SLOT_INDEX ? "right" : "left");
    return ESP_OK;
}

esp_err_t inm441_i2s_mic_start_recording(void)
{
    if (!g_mic_ctx.initialized || !g_mic_ctx.rx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (g_mic_ctx.recording)
    {
        return ESP_OK;
    }

    /* RX first, then the TX clock source: both then start on the same frame boundary. */
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_mic_ctx.rx_handle), TAG, "Failed to enable RX channel");
    ESP_RETURN_ON_ERROR(audio_i2s_bus_start(), TAG, "Failed to start the I2S clock");
    g_mic_ctx.recording = true;
    s_settle_frames     = (MIC_SAMPLE_RATE / 1000U) * MIC_SETTLE_MS;

    ESP_LOGI(TAG, "Recording started");
    return ESP_OK;
}

esp_err_t inm441_i2s_mic_stop_recording(void)
{
    if (!g_mic_ctx.recording)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(i2s_channel_disable(g_mic_ctx.rx_handle), TAG, "Failed to disable RX channel");
    g_mic_ctx.recording = false;

    ESP_LOGI(TAG, "Recording stopped");
    return ESP_OK;
}

esp_err_t inm441_i2s_mic_read_stereo(int32_t *frames, size_t frame_count, size_t *frames_read, uint32_t timeout_ms)
{
    if (!frames || !frames_read || frame_count == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_mic_ctx.initialized || !g_mic_ctx.rx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    size_t    bytes_read = 0;
    esp_err_t ret        = i2s_channel_read(g_mic_ctx.rx_handle,
                                     frames,
                                     frame_count * 2U * sizeof(int32_t),
                                     &bytes_read,
                                     (0U == timeout_ms) ? MIC_READ_TIMEOUT_MS : timeout_ms);

    *frames_read = bytes_read / (2U * sizeof(int32_t));
    if (ESP_OK != ret)
    {
        ESP_LOGW(TAG, "I2S read failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t inm441_i2s_mic_read(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!buffer || !bytes_read || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_mic_ctx.initialized || !g_mic_ctx.rx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    int16_t  *p_out         = (int16_t *)buffer;
    size_t    samples_left  = buffer_size / sizeof(int16_t);
    size_t    samples_done  = 0;
    int32_t   chunk[MIC_CHUNK_FRAMES * 2U];
    esp_err_t ret = ESP_OK;

    /* The bus is stereo: read both slots, keep the one the INMP441 drives and scale 24-bit to 16-bit. */
    while ((samples_left > 0U) && (ESP_OK == ret))
    {
        size_t frames_wanted = (samples_left < MIC_CHUNK_FRAMES) ? samples_left : MIC_CHUNK_FRAMES;
        size_t frames_read   = 0;

        ret = inm441_i2s_mic_read_stereo(chunk, frames_wanted, &frames_read, timeout_ms);
        size_t first = (s_settle_frames < frames_read) ? s_settle_frames : frames_read;
        s_settle_frames -= first;
        for (size_t i = first; i < frames_read; i++)
        {
            int32_t value = chunk[(2U * i) + MIC_SLOT_INDEX] >> MIC_GAIN_SHIFT;
            if (value > INT16_MAX)
            {
                value = INT16_MAX;
            }
            else if (value < INT16_MIN)
            {
                value = INT16_MIN;
            }
            p_out[samples_done++] = (int16_t)value;
        }
        samples_left -= (frames_read - first);
    }

    *bytes_read = samples_done * sizeof(int16_t);
    return ret;
}

esp_err_t inm441_i2s_mic_read_smart(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (bytes_read)
    {
        *bytes_read = 0;
    }
    if (!g_mic_ctx.recording)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = inm441_i2s_mic_read(buffer, buffer_size, bytes_read, timeout_ms);

    /* A partial read on timeout is still usable data. */
    if ((ESP_ERR_TIMEOUT == ret) && bytes_read && (*bytes_read > 0U))
    {
        ret = ESP_OK;
    }
    return ret;
}
