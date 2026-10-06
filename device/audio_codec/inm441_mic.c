#include "inm441_mic.h"

/*******************************************************************************************************************
 * Global Variables
 ******************************************************************************************************************/

static const char *TAG = "INM441_MIC";

inm441_i2s_mic_config_t g_mic_ctx = {
    .rx_handle   = NULL,
    .initialized = false,
    .recording   = false,
};

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Initialize the INM441 microphone I2S channel.
 *
 * This function creates and initializes the I2S RX channel with the configured
 * sample rate, bit depth, DMA buffers, and GPIO pins for the INM441 microphone.
 *
 * @retval ESP_OK                  The microphone I2S channel was initialized successfully.
 * @retval ESP_ERR_INVALID_STATE   The I2S channel could not be initialized.
 * @retval ESP_ERR_NO_MEM          Insufficient memory to create the I2S channel.
 * @retval ESP_ERR_INVALID_ARG     Invalid I2S channel configuration.
 */
esp_err_t inm441_i2s_mic_init(void)
{
    ESP_LOGI(TAG, "Initializing microphone I2S channel...");

    // Channel configuration
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_MIC_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num      = MIC_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num     = MIC_DMA_BUF_LEN;

    // Create RX channel
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &g_mic_ctx.rx_handle), TAG, "Failed to create I2S RX channel");

    // Standard configuration for INMP441 microphone
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(MIC_BITS_PER_SAMPLE, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_SCK_PIN,
            .ws = MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_SD_PIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Configure slot for left channel (INMP441 specific)
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    // Initialize the channel
    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(g_mic_ctx.rx_handle, &std_cfg), TAG, "Failed to initialize I2S RX channel");

    ESP_LOGI(TAG, "Microphone I2S channel initialized for optimized streaming");
    ESP_LOGI(TAG,
             "Configuration: %dHz, %d-bit, mono, %d DMA buffers × %d samples",
             MIC_SAMPLE_RATE,
             16,
             MIC_DMA_BUF_COUNT,
             MIC_DMA_BUF_LEN);
    ESP_LOGI(TAG, "GPIO configuration: SCK=%d, WS=%d, SD=%d", MIC_SCK_PIN, MIC_WS_PIN, MIC_SD_PIN);
    ESP_LOGI(TAG,
             "Buffer optimization: %zu KB total DMA, %zu bytes per read (%.1f ms)",
             MIC_TOTAL_DMA_SIZE / 1024,
             MIC_READ_LEN,
             (float)MIC_READ_LEN / (MIC_SAMPLE_RATE * 2) * 1000);
    g_mic_ctx.initialized = true;
    return ESP_OK;
}

/**
 * @brief Start microphone recording.
 *
 * This function enables the I2S RX channel and starts receiving audio data
 * from the INM441 microphone.
 *
 * @retval ESP_OK                  Recording started successfully or was already running.
 * @retval ESP_ERR_INVALID_STATE   The microphone I2S channel has not been initialized.
 * @retval ESP_ERR_INVALID_ARG     The I2S channel handle is invalid.
 */
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

    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_mic_ctx.rx_handle), TAG, "Failed to enable RX channel");
    g_mic_ctx.recording = true;

    ESP_LOGI(TAG, "Recording started");
    return ESP_OK;
}

/**
 * @brief Stop microphone recording.
 *
 * This function disables the I2S RX channel and stops receiving audio data
 * from the INM441 microphone.
 *
 * @retval ESP_OK                  Recording stopped successfully or was already stopped.
 * @retval ESP_ERR_INVALID_STATE   The I2S RX channel is in an invalid state.
 * @retval ESP_ERR_INVALID_ARG     The I2S channel handle is invalid.
 */
esp_err_t inm441_i2s_mic_stop_recording(void)
{
    if (!g_mic_ctx.recording)
    {
        return ESP_OK;
    }

    if (g_mic_ctx.rx_handle)
    {
        ESP_RETURN_ON_ERROR(i2s_channel_disable(g_mic_ctx.rx_handle), TAG, "Failed to disable RX channel");
    }

    g_mic_ctx.recording = false;
    ESP_LOGI(TAG, "Recording stopped");
    return ESP_OK;
}

/**
 * @brief Read audio data from the microphone I2S channel.
 *
 * This function reads audio data from the I2S RX channel using the specified
 * buffer size and timeout.
 *
 * @param[out] buffer       Pointer to the buffer used to store the audio data.
 * @param[in]  buffer_size  Size of the buffer in bytes.
 * @param[out] bytes_read   Pointer to store the number of bytes actually read.
 * @param[in]  timeout_ms   Read timeout in milliseconds. If set to 0, the
 *                          default microphone read timeout is used.
 *
 * @retval ESP_OK                  Audio data was read successfully.
 * @retval ESP_ERR_INVALID_ARG     Invalid buffer or buffer size.
 * @retval ESP_ERR_INVALID_STATE   The microphone I2S channel has not been initialized.
 * @retval ESP_ERR_TIMEOUT         The read operation timed out.
 * @retval ESP_ERR_INVALID_SIZE    The requested read size is invalid.
 */
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

    *bytes_read = 0;

    // Use configurable timeout if timeout_ms is 0, otherwise use provided timeout
    uint32_t  actual_timeout = (timeout_ms == 0) ? MIC_READ_TIMEOUT_MS : timeout_ms;
    esp_err_t ret
        = i2s_channel_read(g_mic_ctx.rx_handle, buffer, buffer_size, bytes_read, pdMS_TO_TICKS(actual_timeout));
    ESP_LOGW(TAG, "read err=%d, bytes_read=%d", ret, *bytes_read);

    if (ret != ESP_OK)
    {
        ESP_LOGD(TAG, "I2S read error: %s", esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief Get the amount of audio data available for reading.
 *
 * This function returns the configured amount of audio data that can be
 * requested from the microphone I2S channel.
 *
 * @param[out] bytes_available  Pointer to store the number of available bytes.
 *
 * @retval ESP_OK                  The available data size was retrieved successfully.
 * @retval ESP_ERR_INVALID_ARG     The bytes_available pointer is NULL.
 * @retval ESP_ERR_INVALID_STATE   The microphone is not initialized or recording
 *                                 is not active.
 */
esp_err_t inm441_i2s_mic_get_data_size(size_t *bytes_available)
{
    if (!bytes_available)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_mic_ctx.initialized || !g_mic_ctx.rx_handle)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_mic_ctx.recording)
    {
        *bytes_available = 0;
        return ESP_ERR_INVALID_STATE;
    }

    *bytes_available = MIC_READ_LEN;
    return ESP_OK;
}

/**
 * @brief Read audio data using optimized timeout and buffer handling.
 *
 * This function performs a smart I2S read by checking the available data,
 * dynamically calculating the required timeout, and adjusting the requested
 * read size to reduce unnecessary blocking.
 *
 * @param[out] buffer       Pointer to the buffer used to store the audio data.
 * @param[in]  buffer_size  Size of the buffer in bytes.
 * @param[out] bytes_read   Pointer to store the number of bytes actually read.
 * @param[in]  timeout_ms   Maximum read timeout in milliseconds.
 *
 * @retval ESP_OK                  Audio data was read successfully.
 * @retval ESP_ERR_INVALID_ARG     Invalid buffer, bytes_read pointer, or buffer size.
 * @retval ESP_ERR_INVALID_STATE   The microphone is not initialized or recording
 *                                 is not active.
 * @retval ESP_ERR_NOT_FOUND       Not enough data is available for a non-blocking read.
 * @retval ESP_ERR_TIMEOUT         The read operation timed out before receiving data.
 */
esp_err_t inm441_i2s_mic_read_smart(void *buffer, size_t buffer_size, size_t *bytes_read, uint32_t timeout_ms)
{
    if (!buffer || !bytes_read || buffer_size == 0)
    {
        if (bytes_read)
        {
            *bytes_read = 0;
        }
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_mic_ctx.initialized || !g_mic_ctx.rx_handle)
    {
        *bytes_read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_mic_ctx.recording)
    {
        *bytes_read = 0;
        return ESP_ERR_INVALID_STATE;
    }

    size_t    bytes_available = 0;
    esp_err_t ret             = inm441_i2s_mic_get_data_size(&bytes_available);
    if (ret != ESP_OK)
    {
        *bytes_read = 0;
        return ret;
    }

    // Non-blocking và không đủ dữ liệu
    if (bytes_available < MIC_MIN_DATA_READY && timeout_ms == 0)
    {
        *bytes_read = 0;
        return ESP_ERR_NOT_FOUND;
    }

    // Tính timeout thực tế
    uint32_t actual_timeout = timeout_ms;
    if (bytes_available < MIC_MIN_DATA_READY && timeout_ms > 0)
    {
        size_t needed_bytes = (MIC_MIN_DATA_READY > bytes_available) ? (MIC_MIN_DATA_READY - bytes_available) : 0;

        // tránh overflow
        uint32_t expected_fill_time = 0;
        if (needed_bytes > 0)
        {
            expected_fill_time = (uint32_t)(((uint64_t)needed_bytes * 1000) / ((uint64_t)MIC_SAMPLE_RATE * 2));
        }

        uint32_t candidate = expected_fill_time + 50;
        actual_timeout     = (candidate < timeout_ms) ? candidate : timeout_ms;

        ESP_LOGD(TAG,
                 "Smart read: have %zu bytes, need %zu, wait %lums (was %lums)",
                 bytes_available,
                 needed_bytes,
                 actual_timeout,
                 (unsigned long)timeout_ms);
    }

    // Chuẩn bị kích thước đọc: nếu có ít dữ liệu hơn buffer_size thì đọc ít lại để không block lâu
    size_t to_request = buffer_size;
    if (bytes_available > 0 && bytes_available < buffer_size)
    {
        to_request = bytes_available;
    }

    TickType_t timeout_ticks = pdMS_TO_TICKS(actual_timeout);
    *bytes_read              = 0;
    ret                      = i2s_channel_read(g_mic_ctx.rx_handle, buffer, to_request, bytes_read, timeout_ticks);

    if (ret != ESP_OK)
    {
        ESP_LOGD(
            TAG, "Smart I2S read error: %s (requested: %zu, got: %zu)", esp_err_to_name(ret), to_request, *bytes_read);
    }

    // Nếu timeout nhưng vẫn có dữ liệu, coi như thành công (caller có thể xử lý partial)
    if (ret == ESP_ERR_TIMEOUT && *bytes_read > 0)
    {
        ret = ESP_OK;
    }

    return ret;
}
