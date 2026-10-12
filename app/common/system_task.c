#include "system_task.h"

mode_management_t device_mode;

void boot_system_control(void)
{
    app_err_t ret;
    ret = init_device_mode_management(&device_mode);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_CONTROL_TAG, "init_password failed with application error: %d", ret);
        deinit_device_mode_management(&device_mode);
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    ESP_LOGI(SYSTEM_CONTROL_TAG, "System control initialized successfully");
}

void system_control_task(void *pvParameter)
{
#if USED_FREERTOS
    while (1)
    {
        switch (device_mode.mode_state)
        {
            case USER_ENTERING_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User is entering password");
                break;
            case USER_OPEN_LOCK:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User entered true password, opening door");
                break;
            case USER_WRONG_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User entered wrong password, no opening door");
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#else
#endif
}
