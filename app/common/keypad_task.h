#ifndef KEYPAD_TASK_H
#define KEYPAD_TASK_H

#include "keypad.h"
#include "pcf8574.h"
#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "password.h"
#include "sdkconfig.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define SYSTEM_TAG  "SYSTEM"
#define PCF_SDA_PIN CONFIG_PCF8574_DEFAULT_SDA_PIN
#define PCF_SCL_PIN CONFIG_PCF8574_DEFAULT_SCL_PIN
#if USED_FREERTOS
#define PCF_INT_PIN CONFIG_PCF8574_DEFAULT_INT_PIN
#else
#define PCF_INT_PIN GPIO_NUM_NC
#endif
#define PCF_I2C_ADDR   CONFIG_PCF8574_DEFAULT_I2C_ADDR
#define PCF_I2C_SPEED  CONFIG_PCF8574_DEFAULT_CLK_SPEED_HZ
#define PCF_I2C_CLK    I2C_CLK_SRC_DEFAULT
#define PCF_I2C_GLITCH 7U
#define PCF_I2C_PULLUP true

void boot_pcf7584(void);
void keypad_task(void *pvParameter);

#endif
