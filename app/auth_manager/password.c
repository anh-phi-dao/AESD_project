#include "password.h"

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/**
 * @brief Initialize the device password.
 *
 * @param[in]  device_password Pointer to password handle.
 * @param[in]  length          Password length.
 * @param[in]  init_password   Initial password string.
 *
 * @retval APP_SUCCESS             Password initialized successfully.
 * @retval APP_ERR_INVALID_ARGUMENT Invalid password length.
 */
app_err_t init_device_password(password_handle_t *device_password, uint16_t length, const char *init_password)
{
    if (length < 1 || length > MAX_PASSWORD_LENGTH)
    {
        return APP_ERR_INVALID_ARGUMENT;
    }
    if (length > strlen(init_password))
    {
        return APP_ERR_INVALID_ARGUMENT;
    }

    device_password->length        = length;
    device_password->current_index = 0;
    memcpy(device_password->password, init_password, length);
    return APP_SUCCESS;
}

/**
 * @brief Fill password input from keypad state.
 *
 * @param[in]  p_state         Pointer to keypad state.
 * @param[in]  device_password Pointer to password handle.
 *
 * @return Authentication status.
 */
uint8_t fill_password(keypad_state_t *p_state, password_handle_t *device_password)
{
    char    key    = 0;
    uint8_t status = AUTHEN_NONE;

    for (uint8_t key_index = 0U; key_index < (KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT); key_index++)
    {
        if ((p_state->pressed_keys & (uint16_t)(1U << key_index)) != 0U)
        {
            key = s_keypad_keymap[key_index];
            break;
        }
    }

    if (!key)
    {
        return AUTHEN_NONE;
    }

#ifdef DEBUG_PASSWORD
    ESP_LOGI(PASSWORD_DEBUG_TAG, "key mapped :%c", key);
#endif

    device_password->input_password[device_password->current_index] = key;

#ifdef DEBUG_PASSWORD
    ESP_LOGI(PASSWORD_DEBUG_TAG,
             "current input key with index %d :%c",
             device_password->current_index,
             device_password->input_password[device_password->current_index]);
#endif

    device_password->current_index++;

    if (device_password->current_index >= device_password->length)
    {
#ifdef DEBUG_PASSWORD
        ESP_LOGI("fill_password", "Reset input password");
#endif

        device_password->current_index = 0;

        if (!strncmp(device_password->password, device_password->input_password, device_password->length))
        {
            status = AUTHEN_SUCCESS;
        }
        else
        {
            status = AUTHEN_FAILED;
        }

        memset(device_password->input_password, 0, strlen(device_password->input_password));
    }

    return status;
}

/**
 * @brief Deinitialize the device password.
 *
 * @param[in] device_password Pointer to password handle.
 *
 * @retval APP_SUCCESS Password deinitialized successfully.
 */
app_err_t deinit_device_password(password_handle_t *device_password)
{
    if (device_password->length > 0)
    {
        device_password->length = 0;
    }

    memset(device_password->input_password, 0, strlen(device_password->input_password));
    memset(device_password->password, 0, strlen(device_password->password));

    return APP_SUCCESS;
}
