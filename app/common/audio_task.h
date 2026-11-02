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

/* The speaker and the microphone share one I2S bus: whoever plays or records holds this. The key beep only plays
 * when it is free, so it never cuts into a message. False when not taken within wait. */
bool audio_task_speaker_take(TickType_t wait);
void audio_task_speaker_give(void);

#endif
