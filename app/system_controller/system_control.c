#include "system_control.h"

mode_management_t device_mode = {.mode_initialized = false};
password_handle_t user_password;
password_handle_t admin_password;

app_err_t init_device_mode_management(void)
{
    device_mode.mode_id    = USER_MODE;
    device_mode.mode_mutex = xSemaphoreCreateMutex();
    if (device_mode.mode_mutex == NULL)
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
    device_mode.mode_state        = USER_ENTERING_PASSWORD;
    device_mode.mode_password_ptr = &user_password;
    device_mode.mode_initialized  = true;

    return APP_SUCCESS;
}

static app_err_t reset_device_mode_management(void)
{
    if (device_mode.mode_state == false)
    {
        return APP_ERR_INVALID_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(device_mode.mode_mutex, portMAX_DELAY))
    {
        return APP_ERR_TIMEOUT;
    }
#endif
    device_mode.mode_state                       = 0;
    device_mode.mode_password_ptr->current_index = 0;
    memset(device_mode.mode_password_ptr->input_password, 0, sizeof(device_mode.mode_password_ptr->input_password));

#if USED_FREERTOS
    xSemaphoreGive(device_mode.mode_mutex);
#endif

    return APP_SUCCESS;
}

app_err_t change_device_mode(uint8_t mode_id)
{
    if (device_mode.mode_state == false)
    {
        return APP_ERR_INVALID_STATE;
    }

#if USED_FREERTOS
    if (xSemaphoreTake(device_mode.mode_mutex, portMAX_DELAY))
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    device_mode.mode_id = mode_id;

    switch (device_mode.mode_id)
    {
        case USER_MODE:
            device_mode.mode_password_ptr = &user_password;
            break;
        case ADMIN_MODE:
            device_mode.mode_password_ptr = &admin_password;
            break;
        default:
            break;
    }

    reset_device_mode_management();
#if USED_FREERTOS
    xSemaphoreGive(device_mode.mode_mutex);
#endif
    return APP_SUCCESS;
}
