#include "audio_task.h"
#include <math.h>
#include <stdint.h>

#define RECORD_SECONDS_N 3U
#define RECORD_SAMPLES   (MIC_SAMPLE_RATE * RECORD_SECONDS_N)
#define CHUNK_SAMPLES    256U
#define PLAYBACK_GAIN    4 /* the microphone is quiet, raise it for playback */

void boot_audio()
{
    audio_init_management(&audio_management_object);
    ESP_ERROR_CHECK(inm441_i2s_mic_init());
    ESP_ERROR_CHECK(ns4168_speaker_init());
}

static void play_beep(void)
{
    ESP_LOGI(AUDIO_TASK_TAG, ">> Beep...");
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
        ESP_LOGE(AUDIO_TASK_TAG, "speaker write failed");
    }

    ESP_ERROR_CHECK(ns4168_speaker_stop_playback());
    ESP_LOGI(AUDIO_TASK_TAG, "Beep done");
}

void audio_task()
{
    while (1)
    {
        switch (audio_get_state(&audio_management_object))
        {
            case AUDIO_UNSED:
                ESP_LOGI(AUDIO_TASK_TAG, "AUDIO_UNSED");
                break;
            case AUDIO_RECORDING:
                ESP_LOGI(AUDIO_TASK_TAG, "AUDIO_RECORDING");
                break;
            case AUDIO_PLAYING:
                ESP_LOGI(AUDIO_TASK_TAG, "AUDIO_PLAYING");
                break;
            case AUDIO_PLAYING_BEEP:
                ESP_LOGI(AUDIO_TASK_TAG, "AUDIO_PLAYING_BEEP");
                play_beep();
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
