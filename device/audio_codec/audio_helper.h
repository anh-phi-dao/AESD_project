#ifndef AUDIO_H
#define AUDIO_H

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

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

#define AUDIO_BUFFER_SIZE 1024

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
void audio_mono_to_stereo(const int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples);

#endif
