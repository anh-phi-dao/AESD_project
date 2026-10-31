#include "audio_helper.h"

audio_management_t audio_management_object = {.audio_state = AUDIO_UNSED};

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/**
 * @brief Convert mono audio samples to stereo audio samples.
 *
 * Each mono sample is duplicated into both the left and right channels
 * of the stereo output buffer.
 *
 * @param[in]  mono_buffer   Pointer to the input mono audio sample buffer.
 * @param[out] stereo_buffer Pointer to the output stereo audio sample buffer.
 * @param[in]  mono_samples  Number of samples contained in the mono buffer.
 *
 * @return
 *     This function does not return a value.
 */
void audio_mono_to_stereo(const int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples)
{
    if (!mono_buffer || !stereo_buffer || mono_samples == 0)
    {
        return;
    }

    for (size_t i = 0; i < mono_samples; i++)
    {
        stereo_buffer[i * 2]     = mono_buffer[i]; // Left channel
        stereo_buffer[i * 2 + 1] = mono_buffer[i]; // Right channel
    }
}

esp_err_t audio_init_management(audio_management_t *audio_management_object_ptr)
{
    if (!audio_management_object_ptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

#if USED_FREERTOS
    audio_management_object_ptr->audio_mutex = xSemaphoreCreateMutex();
    if (audio_management_object_ptr->audio_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
#endif

    audio_management_object_ptr->audio_state = AUDIO_UNSED;
    return ESP_OK;
}

esp_err_t audio_set_state(audio_management_t *audio_management_object_ptr, uint8_t state)
{
    if (!audio_management_object_ptr)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (state > __AUDIO_MAX_STATE)
    {
        return ESP_ERR_INVALID_ARG;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(audio_management_object_ptr->audio_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }
#endif

    audio_management_object_ptr->audio_state = state;

#if USED_FREERTOS
    xSemaphoreGive(audio_management_object_ptr->audio_mutex);
#endif

    return ESP_OK;
}

uint8_t audio_get_state(audio_management_t *audio_management_object_ptr)
{
    if (!audio_management_object_ptr)
    {
        return AUDIO_ERROR_STATE;
    }
    uint8_t return_state = AUDIO_ERROR_STATE;
#if USED_FREERTOS
    if (xSemaphoreTake(audio_management_object_ptr->audio_mutex, portMAX_DELAY) != pdTRUE)
    {
        return AUDIO_ERROR_STATE;
    }
#endif
    return_state = audio_management_object_ptr->audio_state;

#if USED_FREERTOS
    xSemaphoreGive(audio_management_object_ptr->audio_mutex);
#endif

    return return_state;
}
