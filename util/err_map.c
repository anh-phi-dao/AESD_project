/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "err_map.h"

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Converts ESP-IDF status codes to the application error contract.
 **********************************************************************************************************************/
app_err_t err_map_esp_to_app(esp_err_t esp_error)
{
    switch (esp_error)
    {
        case ESP_OK:
            return APP_SUCCESS;
        case ESP_ERR_INVALID_ARG:
            return APP_ERR_INVALID_ARGUMENT;
        case ESP_ERR_INVALID_STATE:
            return APP_ERR_INVALID_STATE;
        case ESP_ERR_TIMEOUT:
            return APP_ERR_TIMEOUT;
        case ESP_ERR_NO_MEM:
            return APP_ERR_NO_MEMORY;
        case ESP_ERR_NOT_FOUND:
            return APP_ERR_NOT_FOUND;
        case ESP_ERR_NOT_SUPPORTED:
            return APP_ERR_NOT_SUPPORTED;
        default:
            return APP_FAIL;
    }
}
