#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "inm441_mic.h"
#include "ns4168_speaker.h"
#include "audio_helper.h"
#define AP_EN GPIO_NUM_17

const char *TAG = "TEST_AUDIO";
uint8_t     mic_buffer[AUDIO_BUFFER_SIZE];
uint8_t     spk_buffer[AUDIO_BUFFER_SIZE * 2];
size_t      bytes_read;
void        app_main(void)
{
    gpio_config_t io_conf = {.pin_bit_mask = (1ULL << AP_EN),
                             .mode         = GPIO_MODE_OUTPUT,
                             .pull_up_en   = GPIO_PULLUP_DISABLE,
                             .pull_down_en = GPIO_PULLDOWN_DISABLE,
                             .intr_type    = GPIO_INTR_DISABLE};

    gpio_config(&io_conf);

    if (inm441_i2s_mic_init() != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to initiatlize inm441 microphone");
        return;
    }
    if (inm441_i2s_mic_start_recording() != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to start recording in inm441 microphone");
        return;
    }
    if (ns4168_speaker_init() != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to start recording in ns4168 speaker");
        return;
    }
    if (ns4168_speaker_start_playback() != ESP_OK)
    {
        ESP_LOGI(TAG, "Failed to start playing in ns4168 speaker");
        return;
    }
    while (1)
    {
        bytes_read    = 0;
        esp_err_t ret = inm441_i2s_mic_read(mic_buffer, AUDIO_BUFFER_SIZE, &bytes_read, 120);
        if (ret == ESP_OK && bytes_read > 0)
        {
            // Convert mono to stereo
            audio_mono_to_stereo((int16_t *)mic_buffer, (int16_t *)spk_buffer, bytes_read / 2);

            size_t bytes_written = 0;
            ns4168_speaker_write(spk_buffer, bytes_read * 2, &bytes_written, 100);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
        ESP_LOGI(TAG, "Audio loopback task stopped");
    }
}
