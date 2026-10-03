#ifndef AUDIO_H
#define AUDIO_H

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AUDIO_BUFFER_SIZE 1024

void audio_mono_to_stereo(const int16_t *mono_buffer, int16_t *stereo_buffer, size_t mono_samples);

#endif
