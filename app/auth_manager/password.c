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
app_err_t init_password(password_handle_t *device_password, uint16_t length, const char *init_password)
{
    if (length < 1 || length > MAX_PASSWORD_LENGTH)
    {
        return APP_ERR_INVALID_ARGUMENT;
    }
    if (length > strlen(init_password))
    {
        return APP_ERR_INVALID_ARGUMENT;
    }

    device_password->current_index = 0;
    device_password->full          = false;
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
uint8_t fill_password(char key, password_handle_t *device_password)
{
    uint8_t status = FILLING_PASSWORD;

    if (device_password->full)
    {
        return NO_MORE_SPACE;
    }

    if (!key)
    {
        return ERROR_FILLING_PASSWORD;
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

    if (device_password->current_index > MAX_PASSWORD_LENGTH)
    {
        device_password->full = true;
        ESP_LOGI(PASSWORD_DEBUG_TAG, "User should validate password or admin should validate or change password");
        status = FILLING_ENOUGH;
    }

    return status;
}

uint8_t validate_password(password_handle_t *device_password)
{
    /*simple validate password*/
    uint8_t status = AUTHEN_FAILED;
    if (!strcmp(device_password->input_password, device_password->password))
    {
        status = AUTHEN_SUCCESS;
    }
    memset(device_password->input_password, 0, strlen(device_password->input_password));
    device_password->current_index = 0;
    return status;
    /*TODO : Sử dụng các thuật toán bảo mật để validate trên các chuỗi đã được mã hóa*/
}

app_err_t change_password(password_handle_t *device_password)
{
    size_t input_length = strlen(device_password->input_password);
    if (!input_length || input_length > MAX_PASSWORD_LENGTH)
    {
        return APP_ERR_INVALID_ARGUMENT;
    }
    memset(device_password->password, 0, strlen(device_password->password));
    memcpy(device_password->password, device_password->input_password, strlen(device_password->input_password));
    return APP_SUCCESS;
}

/**
 * @brief Deinitialize the device password.
 *
 * @param[in] device_password Pointer to password handle.
 *
 * @retval APP_SUCCESS Password deinitialized successfully.
 */
app_err_t deinit_password(password_handle_t *device_password)
{

    memset(device_password->input_password, 0, strlen(device_password->input_password));
    memset(device_password->password, 0, strlen(device_password->password));

    return APP_SUCCESS;
}
