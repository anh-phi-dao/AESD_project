#ifndef RFID_TASK_H
#define RFID_TASK_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "pn532.h"
#include "pn532_driver.h"
#include "pn532_driver_i2c.h"
#include "system_control.h"
#include <stdbool.h>

#define RFID_TAG "RFID"

void boot_rfid(void);

/* True when the PN532 answered at boot (and owns the I2C bus). */
bool rfid_task_is_ready(void);
void rfid_task(void *pvParameter);

#endif
