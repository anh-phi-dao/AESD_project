/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdint.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "inm441_mic.h"
#include "ns4168_speaker.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/
#define TEST_RECORD_PLAYBACK 0
#define TEST_BEEP            1

#define TAG              "TEST_AUDIO"
#define RECORD_SECONDS_N 3U
#define RECORD_SAMPLES   (MIC_SAMPLE_RATE * RECORD_SECONDS_N)
#define CHUNK_SAMPLES    256U
#define PLAYBACK_GAIN    4 /* the microphone is quiet, raise it for playback */

/***********************************************************************************************************************
 * Private variables
 **********************************************************************************************************************/

static int16_t s_record[RECORD_SAMPLES];

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/
static void test_record(void)
{
    size_t total = 0;

    ESP_ERROR_CHECK(inm441_i2s_mic_start_recording());
    ESP_LOGI(TAG, ">> SPEAK NOW for %u seconds...", (unsigned)RECORD_SECONDS_N);

    while (total < RECORD_SAMPLES)
    {
        size_t want       = ((RECORD_SAMPLES - total) < CHUNK_SAMPLES) ? (RECORD_SAMPLES - total) : CHUNK_SAMPLES;
        size_t bytes_read = 0;

        if (ESP_OK != inm441_i2s_mic_read_smart(&s_record[total], want * sizeof(int16_t), &bytes_read, 200))
        {
            ESP_LOGE(TAG, "mic read failed");
            break;
        }
        total += bytes_read / sizeof(int16_t);
    }

    ESP_ERROR_CHECK(inm441_i2s_mic_stop_recording());
    ESP_LOGI(TAG, "Recorded %u samples", (unsigned)total);
}

static void test_playback(void)
{
    int16_t stereo[CHUNK_SAMPLES * 2U];

    ESP_LOGI(TAG, ">> Playing back...");
    ESP_ERROR_CHECK(ns4168_speaker_start_playback());

    for (size_t pos = 0; pos < RECORD_SAMPLES; pos += CHUNK_SAMPLES)
    {
        for (size_t i = 0; i < CHUNK_SAMPLES; i++)
        {
            int32_t v           = (int32_t)s_record[pos + i] * PLAYBACK_GAIN;
            v                   = (v > INT16_MAX) ? INT16_MAX : ((v < INT16_MIN) ? INT16_MIN : v);
            stereo[2U * i]      = (int16_t)v;
            stereo[2U * i + 1U] = (int16_t)v;
        }
        size_t written = 0;
        if (ESP_OK != ns4168_speaker_write(stereo, sizeof(stereo), &written, 200))
        {
            ESP_LOGE(TAG, "speaker write failed");
            break;
        }
    }

    ESP_ERROR_CHECK(ns4168_speaker_stop_playback());
    ESP_LOGI(TAG, "Playback done");
}

static void test_beep(void)
{
    ESP_LOGI(TAG, ">> Beep...");
    ESP_ERROR_CHECK(ns4168_speaker_start_playback());

    int16_t beep[CHUNK_SAMPLES * 2U];
    for (size_t i = 0; i < CHUNK_SAMPLES; i++)
    {
        float   t         = (float)i / MIC_SAMPLE_RATE;
        float   f         = 1500.0f; // 1 kHz beep
        float   a         = 0.5f;    // amplitude
        float   s         = a * sinf(2.0f * M_PI * f * t);
        int16_t sample    = (int16_t)(s * INT16_MAX);
        beep[2U * i]      = sample;
        beep[2U * i + 1U] = sample;
    }

    size_t written = 0;
    if (ESP_OK != ns4168_speaker_write(beep, sizeof(beep), &written, 200))
    {
        ESP_LOGE(TAG, "speaker write failed");
    }

    ESP_ERROR_CHECK(ns4168_speaker_stop_playback());
    ESP_LOGI(TAG, "Beep done");
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

void app_main(void)
{
#if TEST_RECORD_PLAYBACK
    ESP_ERROR_CHECK(inm441_i2s_mic_init());
#endif
    ESP_ERROR_CHECK(ns4168_speaker_init());

    while (1)
    {
#if TEST_RECORD_PLAYBACK
        vTaskDelay(pdMS_TO_TICKS(2000));
        test_record();
        test_playback();
#endif
#if TEST_BEEP
        test_beep();
#endif
    }
}
