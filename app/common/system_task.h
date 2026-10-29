#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_control.h"
#include "esp_log.h"

#define SYSTEM_CONTROL_TAG     "SYSTEM_CONTROL"
#define SYSTEM_CONTROL_TICK_MS 100U /* Timeouts and countdowns of system_control. */

/* Starts system_control (docs/ui_data.md) and connects its decisions to the event log, the door and the audio.
 * Call before the keypad, the card reader and the screen flow. */
void boot_system_control(void);
void system_control_task(void *pvParameter);
#endif
