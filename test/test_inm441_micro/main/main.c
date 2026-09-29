#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "inm441_mic.h"
const char *TAG = "TEST_MIC";

uint8_t  mic_buffer[2];
uint16_t result;
size_t   byte_read;
void     app_main(void)
{
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
    while (1)
    {
        memset(mic_buffer, 0, sizeof(mic_buffer));
        if (inm441_i2s_mic_read_smart(mic_buffer, sizeof(mic_buffer), &byte_read, 1000) != ESP_OK)
        {
            return;
        }
        memcpy(&result, mic_buffer, sizeof(mic_buffer));
        ESP_LOGI(TAG, "Result %d", result);
        ESP_LOGI(TAG, "Byte read %lu", byte_read);
    }
}
