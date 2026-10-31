#ifndef AUDIO_TASK_H
#define AUDIO_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "inm441_mic.h"
#include "ns4168_speaker.h"
#include "audio_helper.h"
#include "system_export.h"

#define AUDIO_TASK_TAG "AUDIO_TASK"

void boot_audio();
void audio_task();

#endif
