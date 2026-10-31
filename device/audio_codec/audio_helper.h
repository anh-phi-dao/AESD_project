#ifndef AUDIO_H
#define AUDIO_H

#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

#define AUDIO_BUFFER_SIZE 1024

/*******************************************************************************************************************
 * Enum and typdef definitions
 ******************************************************************************************************************/
struct audio_management
{
    SemaphoreHandle_t audio_mutex;
    uint8_t           audio_state;
};

typedef struct audio_management audio_management_t;

enum
{
    AUDIO_ERROR_STATE = -1,
    AUDIO_UNSED,
    AUDIO_RECORDING,
    AUDIO_PLAYING,
    AUDIO_PLAYING_BEEP,
    __AUDIO_MAX_STATE
};

extern audio_management_t audio_management_object;
/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Convert mono audio samples to stereo audio samples.
 *
 * Each mono sample is duplicated into the left and right channels of the
 * stereo output buffer.
 *
 * @param[in]  mono_buffer   Pointer to the input mono audio sample buffer.
 * @param[out] stereo_buffer Pointer to the output stereo audio sample buffer.
 * @param[in]  mono_samples  Number of samples contained in the mono buffer.
 *
 * @return
 *     This function does not return a value.
 */
void      audio_mono_to_stereo(const int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples);
esp_err_t audio_init_management(audio_management_t *audio_management_object_ptr);
esp_err_t audio_set_state(audio_management_t *audio_management_object_ptr, uint8_t state);
uint8_t   audio_get_state(audio_management_t *audio_management_object_ptr);
#endif
