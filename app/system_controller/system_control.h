#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_err.h"
#include "esp_log.h"
#include "password.h"
#include "keypad_config.h"

/*There are 12 button, means that maximum action of each mode is 12*/
#define MAXIMUM_ALLOWED_ACTION 12

enum
{
    USER_MODE,
    ADMIN_MODE
};

enum
{
    USER_ENTERING_PASSWORD = 0,
    USER_OPEN_LOCK,
    USER_WRONG_PASSWORD,
};

enum
{
    ADMIN_LOGGING_IN = 0,
    ADMIN_LOGGED_IN,
    ADMIN_WRONG_PASSWORD,
    ADMIN_CHANGE_PASWORD,
    ADMIN_ADD_UID,
    ADMIN_DELETE_UID,
    ADMIN_ADD_FIGER_PRINT,
    ADMIN_DELETE_FINGER_PRINT,
    ADMIN_RECORDING,
    ADMIN_PLAYING
};

typedef struct
{
    SemaphoreHandle_t  mode_mutex;
    password_handle_t *mode_password_ptr;
    uint8_t            mode_id;
    uint8_t            mode_state;
    bool               mode_initialized;

} mode_management_t;

app_err_t init_device_mode_management(void);
app_err_t change_device_mode(uint8_t mode_id);
#endif
