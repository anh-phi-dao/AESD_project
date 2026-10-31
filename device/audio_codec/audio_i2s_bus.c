/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "audio_i2s_bus.h"

#include "esp_check.h"
#include "esp_log.h"

/***********************************************************************************************************************
 * Private variables
 **********************************************************************************************************************/

static const char *TAG = "AUDIO_I2S_BUS";

static i2s_chan_handle_t s_tx_handle = NULL;
static i2s_chan_handle_t s_rx_handle = NULL;
static bool              s_started   = false;

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

esp_err_t audio_i2s_bus_init(i2s_chan_handle_t *p_tx, i2s_chan_handle_t *p_rx)
{
    if (NULL == s_tx_handle)
    {
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
        chan_cfg.dma_desc_num      = AUDIO_I2S_DMA_DESC;
        chan_cfg.dma_frame_num     = AUDIO_I2S_DMA_FRAMES;
        chan_cfg.auto_clear_after_cb = true; /* output silence instead of repeating old data on TX underrun */

        ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_handle, &s_rx_handle), TAG, "Failed to create I2S channels");

        i2s_std_config_t std_cfg = {
            .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_I2S_SAMPLE_RATE),
            .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
            .gpio_cfg = {.mclk         = I2S_GPIO_UNUSED,
                         .bclk         = AUDIO_I2S_BCLK_PIN,
                         .ws           = AUDIO_I2S_WS_PIN,
                         .dout         = AUDIO_I2S_DOUT_PIN,
                         .din          = AUDIO_I2S_DIN_PIN,
                         .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}},
        };

        /* TX and RX share BCLK/WS, so both use the identical 32-bit stereo slot format (64 BCLK per frame). */
        esp_err_t ret = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
        if (ESP_OK == ret)
        {
            ret = i2s_channel_init_std_mode(s_rx_handle, &std_cfg);
        }
        if (ESP_OK != ret)
        {
            ESP_LOGE(TAG, "Failed to init I2S standard mode: %s", esp_err_to_name(ret));
            (void)i2s_del_channel(s_tx_handle);
            (void)i2s_del_channel(s_rx_handle);
            s_tx_handle = NULL;
            s_rx_handle = NULL;
            return ret;
        }

        ESP_LOGI(TAG,
                 "I2S full-duplex ready: %d Hz, BCLK=%d, WS=%d, DOUT=%d, DIN=%d",
                 AUDIO_I2S_SAMPLE_RATE,
                 AUDIO_I2S_BCLK_PIN,
                 AUDIO_I2S_WS_PIN,
                 AUDIO_I2S_DOUT_PIN,
                 AUDIO_I2S_DIN_PIN);
    }

    if (NULL != p_tx)
    {
        *p_tx = s_tx_handle;
    }
    if (NULL != p_rx)
    {
        *p_rx = s_rx_handle;
    }
    return ESP_OK;
}

esp_err_t audio_i2s_bus_start(void)
{
    if (s_started)
    {
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(NULL != s_tx_handle, ESP_ERR_INVALID_STATE, TAG, "I2S bus not initialized");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_handle), TAG, "Failed to start the I2S clock");
    s_started = true;
    return ESP_OK;
}
