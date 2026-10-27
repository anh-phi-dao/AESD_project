#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include <math.h>
#include "inm441_mic.h"
#include "ns4168_speaker.h"
#include "audio_helper.h"
#define TEST_BEEP_SOUND  0
#define TEST_MIC_AND_SPK 1
#define NS4168_CTR       GPIO_NUM_41

#if TEST_BEEP_SOUND == 1
const char *TAG = "TEST_AUDIO";
// uint8_t     mic_buffer[AUDIO_BUFFER_SIZE];
int16_t spk_buffer[AUDIO_BUFFER_SIZE * 2];
// size_t  bytes_read;
void app_main(void)
{
    // gpio_config_t io_conf = {.pin_bit_mask = (1ULL << NS4168_CTR),
    //                          .mode         = GPIO_MODE_OUTPUT,
    //                          .pull_up_en   = GPIO_PULLUP_DISABLE,
    //                          .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //                          .intr_type    = GPIO_INTR_DISABLE};

    // gpio_config(&io_conf);
    // gpio_set_level(NS4168_CTR, 0);

    // if (inm441_i2s_mic_init() != ESP_OK)
    // {
    //     ESP_LOGI(TAG, "Failed to initiatlize inm441 microphone");
    //     return;
    // }
    // if (inm441_i2s_mic_start_recording() != ESP_OK)
    // {
    //     ESP_LOGI(TAG, "Failed to start recording in inm441 microphone");
    //     return;
    // }
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
        for (size_t i = 0; i < AUDIO_BUFFER_SIZE; i++)
        {
            int16_t sample        = (int16_t)(8000 * sinf(2.0f * 3.14159265f * 1000 * i / SPK_SAMPLE_RATE));
            spk_buffer[i * 2]     = sample;
            spk_buffer[i * 2 + 1] = sample;
        }

        ns4168_speaker_adjust_volume(spk_buffer, AUDIO_BUFFER_SIZE * 2, 100); // Adjust volume to 100%
        // bytes_read = 0;
        // esp_err_t ret = inm441_i2s_mic_read(mic_buffer, AUDIO_BUFFER_SIZE, &bytes_read, 120);
        // if (ret == ESP_OK && bytes_read > 0)
        // {
        // Convert mono to stereo
        // audio_mono_to_stereo((int16_t *)mic_buffer, (int16_t *)spk_buffer, bytes_read / 2);

        size_t    bytes_written = 0;
        esp_err_t ret           = ns4168_speaker_write(spk_buffer, sizeof(spk_buffer), &bytes_written, 100);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to write audio data to speaker %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(30));
        ESP_LOGI(TAG, "Audio loopback task stopped");
    }
}
#endif

#if TEST_MIC_AND_SPK == 1
const char *TAG = "TEST_AUDIO";
uint8_t     mic_buffer[AUDIO_BUFFER_SIZE];
uint8_t     spk_buffer[AUDIO_BUFFER_SIZE * 2];
size_t      bytes_read;
void        app_main(void)
{
    // gpio_config_t io_conf = {.pin_bit_mask = (1ULL << NS4168_CTR),
    //                          .mode         = GPIO_MODE_OUTPUT,
    //                          .pull_up_en   = GPIO_PULLUP_DISABLE,
    //                          .pull_down_en = GPIO_PULLDOWN_DISABLE,
    //                          .intr_type    = GPIO_INTR_DISABLE};

    // gpio_config(&io_conf);

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
            ns4168_speaker_adjust_volume((int16_t *)spk_buffer, AUDIO_BUFFER_SIZE * 2, 100); // Adjust volume to 100%
            size_t bytes_written = 0;
            ns4168_speaker_write(spk_buffer, bytes_read * 2, &bytes_written, 100);
        }
        vTaskDelay(pdMS_TO_TICKS(30));
        ESP_LOGI(TAG, "Audio loopback task stopped");
    }
}
#endif
