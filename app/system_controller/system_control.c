
#include "system_control.h"

static app_err_t update_state_for_user_mode(mode_management_t *mode, uint8_t new_mode_state, bool use_common_state);
static app_err_t update_state_for_admin_mode(mode_management_t *mode, uint8_t new_mode_state, bool use_common_state);

/***********************************************************************************************************************
 * Global Variables
 **********************************************************************************************************************/

password_handle_t user_password;
password_handle_t admin_password;

/***********************************************************************************************************************
 * Private APIs
 **********************************************************************************************************************/

/**
 * @brief Reset the current device mode state.
 *
 * Resets the mode state, current password input index, and input password
 * buffer associated with the selected mode.
 *
 * @param[in,out] mode Pointer to the mode management structure.
 *
 * @return
 *     - APP_SUCCESS            : Mode state reset successfully.
 *     - APP_ERR_INVALID_STATE  : Mode management is not in a valid state.
 *     - APP_ERR_TIMEOUT        : Failed to acquire the mode mutex.
 */
app_err_t reset_device_mode_management(mode_management_t *mode)
{
    if (mode->mode_initialized == false)
    {
        return APP_ERR_INVALID_STATE;
    }

    mode->mode_password_ptr->current_index = 0;
    memset(mode->mode_password_ptr->input_password, 0, strlen(mode->mode_password_ptr->input_password));

    switch (mode->mode_id)
    {
        case USER_MODE:
            mode->mode_state = USER_ENTERING_PASSWORD;
            break;
        case ADMIN_MODE:
            mode->mode_state = ADMIN_LOGGING_IN;
            break;
        default:
            break;
    }
    mode->mode_tmp_lock = false;

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/**
 * @brief Initialize device mode management.
 *
 * Initializes the mode management structure, creates the mode mutex when
 * FreeRTOS support is enabled, initializes the user and administrator
 * passwords, and selects the initial user mode.
 *
 * @param[in,out] mode Pointer to the mode management structure.
 *
 * @return
 *     - APP_SUCCESS            : Mode management initialized successfully.
 *     - APP_ERR_INVALID_STATE  : Mutex creation or password initialization
 *                                failed.
 */
app_err_t init_device_mode_management(mode_management_t *mode)
{
    mode->mode_initialized = false;
    mode->mode_id          = USER_MODE;

#if USED_FREERTOS
    mode->mode_mutex = xSemaphoreCreateMutex();
#endif

    if (mode->mode_mutex == NULL)
    {
        return APP_ERR_INVALID_STATE;
    }

    /*Chỗ này sẽ thế bằng việc đọc từ flash hay sd_card*/
    if (init_password(&user_password, 7, "1234567") != APP_SUCCESS)
    {
        return APP_ERR_INVALID_STATE;
    }

    /*Chỗ này sẽ thế bằng việc đọc từ flash hay sd_card*/
    if (init_password(&admin_password, 7, "136*#02") != APP_SUCCESS)
    {
        return APP_ERR_INVALID_STATE;
    }

    mode->mode_state        = USER_ENTERING_PASSWORD;
    mode->mode_password_ptr = &user_password;
    mode->mode_update_state = update_state_for_user_mode;
    mode->mode_initialized  = true;
    mode->mode_tmp_lock     = false;
    mode->mode_lock_forever = false;
    return APP_SUCCESS;
}

/**
 * @brief Change the active device mode.
 *
 * Updates the selected mode and switches the active password pointer
 * to the password associated with the requested mode.
 *
 * @param[in,out] mode    Pointer to the mode management structure.
 * @param[in]     mode_id Identifier of the mode to activate.
 *
 * @return
 *     - APP_SUCCESS            : Mode change completed.
 *     - APP_ERR_INVALID_STATE  : Mode management is not in a valid state.
 *     - APP_ERR_TIMEOUT        : Failed to acquire the mode mutex.
 */
app_err_t change_device_mode(mode_management_t *mode, mode_enum_t mode_id)
{
    if (mode->mode_initialized == false)
    {
        return APP_ERR_INVALID_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(mode->mode_mutex, portMAX_DELAY) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    mode->mode_id = mode_id;

    switch (mode->mode_id)
    {
        case USER_MODE:
            mode->mode_password_ptr = &user_password;
            mode->mode_update_state = update_state_for_user_mode;
            mode->mode_state        = USER_ENTERING_PASSWORD;
            break;

        case ADMIN_MODE:
            mode->mode_password_ptr = &admin_password;
            mode->mode_update_state = update_state_for_admin_mode;
            mode->mode_state        = ADMIN_LOGGED_IN;
            break;

        default:
            break;
    }

    reset_device_mode_management(mode);

#if USED_FREERTOS
    xSemaphoreGive(mode->mode_mutex);
#endif

    return APP_SUCCESS;
}

/**
 * @brief Deinitialize device mode management.
 *
 * Deletes the mode mutex when FreeRTOS support is enabled and clears
 * the active password pointer and initialization flag.
 *
 * @param[in,out] mode Pointer to the mode management structure.
 *
 * @return
 *     This function does not return a value.
 */
void deinit_device_mode_management(mode_management_t *mode)
{
#if USED_FREERTOS
    vSemaphoreDelete(mode->mode_mutex);
    mode->mode_mutex = NULL;
#endif

    mode->mode_password_ptr = NULL;
    mode->mode_initialized  = false;
    mode->mode_id           = 0;
}

static app_err_t update_state_for_user_mode(mode_management_t *mode, uint8_t new_mode_state, bool use_common_state)
{
    if (mode->mode_initialized == false)
    {
        return APP_ERR_INVALID_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(mode->mode_mutex, portMAX_DELAY) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    if (mode->mode_id != USER_MODE)
    {
        return APP_ERR_INVALID_STATE;
    }

    if (use_common_state == true)
    {
        switch (new_mode_state)
        {
            case LOGGING_IN:
                mode->mode_state = USER_ENTERING_PASSWORD;
                break;
            case LOGGED_IN:
                mode->mode_state = USER_OPEN_LOCK;
                break;
            case WRONG_PASSWORD:
                mode->mode_state = USER_WRONG_PASSWORD;
                break;
            case LOCK_SYSTEM:
                mode->mode_state = USER_LOCK_SYSTEM;
                break;
            default:
#if USED_FREERTOS
                xSemaphoreGive(mode->mode_mutex);
#endif
                return APP_ERR_INVALID_ARGUMENT;
        }
#if USED_FREERTOS
        xSemaphoreGive(mode->mode_mutex);
#endif
        return APP_SUCCESS;
    }

    switch (new_mode_state)
    {
        case USER_ENTERING_PASSWORD:
            mode->mode_state = USER_ENTERING_PASSWORD;
            break;
        case USER_OPEN_LOCK:
            mode->mode_state = USER_OPEN_LOCK;
            break;
        case USER_WRONG_PASSWORD:
            mode->mode_state = USER_WRONG_PASSWORD;
            break;
        case USER_LOCK_SYSTEM:
            mode->mode_state = USER_LOCK_SYSTEM;
            break;
        default:
#if USED_FREERTOS
            xSemaphoreGive(mode->mode_mutex);
#endif
            return APP_ERR_INVALID_ARGUMENT;
            break;
    }

#if USED_FREERTOS
    xSemaphoreGive(mode->mode_mutex);
#endif
    return APP_SUCCESS;
}

static app_err_t update_state_for_admin_mode(mode_management_t *mode, uint8_t new_mode_state, bool use_common_state)
{
    if (mode->mode_initialized == false)
    {
        return APP_ERR_INVALID_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(mode->mode_mutex, portMAX_DELAY) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    if (mode->mode_id != ADMIN_MODE)
    {
        return APP_ERR_INVALID_STATE;
    }

    if (use_common_state == true)
    {
        switch (new_mode_state)
        {
            case LOGGING_IN:
                mode->mode_state = ADMIN_LOGGING_IN;
                break;
            case LOGGED_IN:
                mode->mode_state = ADMIN_LOGGED_IN;
                break;
            case WRONG_PASSWORD:
                mode->mode_state = ADMIN_WRONG_PASSWORD;
                break;
            case LOCK_SYSTEM:
                mode->mode_state = ADMIN_LOCK_SYSTEM;
                break;
            default:
#if USED_FREERTOS
                xSemaphoreGive(mode->mode_mutex);
#endif
                return APP_ERR_INVALID_ARGUMENT;
                break;
        }
#if USED_FREERTOS
        xSemaphoreGive(mode->mode_mutex);
#endif
        return APP_SUCCESS;
    }

    switch (new_mode_state)
    {
        case ADMIN_LOGGING_IN:
            mode->mode_state = ADMIN_LOGGING_IN;
            break;
        case ADMIN_LOGGED_IN:
            mode->mode_state = ADMIN_LOGGED_IN;
            break;
        case ADMIN_WRONG_PASSWORD:
            mode->mode_state = ADMIN_WRONG_PASSWORD;
            break;
        case ADMIN_LOCK_SYSTEM:
            mode->mode_state = ADMIN_LOCK_SYSTEM;
            break;
        /*TODO: thêm các case khác cho state bổ sung của admin*/
        default:
#if USED_FREERTOS
            xSemaphoreGive(mode->mode_mutex);
#endif
            return APP_ERR_INVALID_ARGUMENT;
            break;
    }

#if USED_FREERTOS
    xSemaphoreGive(mode->mode_mutex);
#endif
    return APP_SUCCESS;
}

uint8_t get_device_mode_common_state(mode_management_t *mode)
{
    if (mode->mode_initialized == false)
    {
        return ERROR_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(mode->mode_mutex, portMAX_DELAY) != pdTRUE)
    {
        return ERROR_STATE;
    }
#endif
    uint8_t state = ERROR_STATE;
    if (mode->mode_state == USER_ENTERING_PASSWORD || mode->mode_state == ADMIN_LOGGING_IN)
    {
        state = LOGGING_IN;
    }
    else if (mode->mode_state == USER_OPEN_LOCK || mode->mode_state == ADMIN_LOGGED_IN)
    {
        state = LOGGED_IN;
    }
    else if (mode->mode_state == USER_WRONG_PASSWORD || mode->mode_state == ADMIN_WRONG_PASSWORD)
    {
        state = WRONG_PASSWORD;
    }
    else if (mode->mode_state == USER_LOCK_SYSTEM || mode->mode_state == ADMIN_LOCK_SYSTEM)
    {
        state = LOCK_SYSTEM;
    }

#if USED_FREERTOS
    xSemaphoreGive(mode->mode_mutex);
#endif
    return state;
}

bool get_device_lock_state(mode_management_t *mode)
{
    bool lock_state = false;
    if (mode->mode_tmp_lock || mode->mode_lock_forever)
    {
        lock_state = true;
    }
    return lock_state;
}
