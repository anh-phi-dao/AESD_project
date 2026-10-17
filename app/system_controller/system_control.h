
#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_err.h"
#include "esp_log.h"
#include "password.h"
#include "keypad_config.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

/*There are 12 button, means that maximum action of each mode is 12*/
#define MAXIMUM_ALLOWED_ACTION 12

/*******************************************************************************************************************
 * Typedef definitions
 ******************************************************************************************************************/

typedef enum
{
    USER_MODE = 1,
    ADMIN_MODE
} mode_enum_t;

enum
{
    USER_ENTERING_PASSWORD = 0,
    USER_OPEN_LOCK,
    USER_WRONG_PASSWORD,
    USER_LOCK_SYSTEM
};

enum
{
    ADMIN_LOGGING_IN = 4,
    ADMIN_LOGGED_IN,
    ADMIN_WRONG_PASSWORD,
    ADMIN_LOCK_SYSTEM,
    ADMIN_CHANGE_PASWORD,
    ADMIN_ADD_UID,
    ADMIN_DELETE_UID,
    ADMIN_ADD_FIGER_PRINT,
    ADMIN_DELETE_FINGER_PRINT,
    ADMIN_RECORDING,
    ADMIN_PLAYING
};

enum
{
    ERROR_STATE = -1,
    LOGGING_IN,
    LOGGED_IN,
    WRONG_PASSWORD,
    LOCK_SYSTEM
};

typedef struct st_mode_management
{
    SemaphoreHandle_t  mode_mutex;
    password_handle_t *mode_password_ptr;
    app_err_t (*mode_update_state)(struct st_mode_management *mode, uint8_t new_mode_state, bool use_common_state);
    uint8_t mode_id;
    uint8_t mode_state;
    bool    mode_initialized;
    bool    mode_tmp_lock;
    bool    mode_lock_forever;
} mode_management_t;

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Initialize device mode management.
 *
 * @param[in,out] mode Pointer to the mode management structure.
 *
 * @return
 *     - APP_SUCCESS           : Initialization completed successfully.
 *     - APP_ERR_INVALID_STATE : Mutex creation or password initialization
 *                               failed.
 */
app_err_t init_device_mode_management(mode_management_t *mode);

/**
 * @brief Change the active device mode.
 *
 * @param[in,out] mode    Pointer to the mode management structure.
 * @param[in]     mode_id Identifier of the mode to activate.
 *
 * @return
 *     - APP_SUCCESS           : Mode change completed.
 *     - APP_ERR_INVALID_STATE : Mode management is not in a valid state.
 *     - APP_ERR_TIMEOUT       : Failed to acquire the mode mutex.
 */
app_err_t change_device_mode(mode_management_t *mode, mode_enum_t mode_id);

/**
 * @brief Deinitialize device mode management.
 *
 * @param[in,out] mode Pointer to the mode management structure.
 *
 * @return
 *     This function does not return a value.
 */
void deinit_device_mode_management(mode_management_t *mode);

uint8_t get_device_mode_common_state(mode_management_t *mode);

app_err_t reset_device_mode_management(mode_management_t *mode);

bool get_device_lock_state(mode_management_t *mode);

#endif
