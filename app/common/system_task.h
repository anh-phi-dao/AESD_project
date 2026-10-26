#ifndef SYSTEM_TASK_H
#define SYSTEM_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_control.h"
#include "esp_timer.h"
#include "esp_log.h"

#define SYSTEM_CONTROL_TAG "SYSTEM_CONTROL"
void boot_system_control(void);
void system_control_task(void *pvParameter);
#endif
