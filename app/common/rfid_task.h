#ifndef RFID_TASK_H
#define RFID_TASK_H

#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "driver/gpio.h"
#include "esp_log.h"
#include "pn532.h"
#include "pn532_driver.h"
#include "pn532_driver_i2c.h"

void boot_rfid(void);
void rfid_task(void *pvParameter);

#endif
