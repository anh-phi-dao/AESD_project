#include "system_task.h"

static void                   timer_callback(void *param);
mode_management_t             device_mode;
const esp_timer_create_args_t system_timer_args = {.callback = &timer_callback, .name = "System control timer"};
esp_timer_handle_t            system_control_timer_handler;

static void timer_callback(void *param)
{
    switch (device_mode.mode_state)
    {
        case USER_OPEN_LOCK:
            /*Đóng khóa*/
            reset_device_mode_management(&device_mode);
            break;
        case USER_WRONG_PASSWORD:
            reset_device_mode_management(&device_mode);
            break;
        case ADMIN_WRONG_PASSWORD:
            reset_device_mode_management(&device_mode);
            break;
        case USER_LOCK_SYSTEM:
            if (!device_mode.mode_lock_forever)
            {
                reset_device_mode_management(&device_mode);
            }
            break;
        case ADMIN_LOCK_SYSTEM:
            if (!device_mode.mode_lock_forever)
            {
                reset_device_mode_management(&device_mode);
            }
            break;
        default:
            break;
    }
}

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
    ESP_ERROR_CHECK(esp_timer_create(&system_timer_args, &system_control_timer_handler));
    ESP_LOGI(SYSTEM_CONTROL_TAG, "%s initialized successfully ", system_timer_args.name);
}

void system_user_open_lock_action(void)
{
    /*Mở khóa*/
    /*Chạy timer giữ khóa khoảng 10s*/
    if (esp_timer_is_active(system_control_timer_handler) == false)
    {
        ESP_ERROR_CHECK(esp_timer_start_periodic(system_control_timer_handler, 10000000));
    }
}

void system_common_wrong_password(void)
{
    if (device_mode.mode_password_ptr->failed_valid_count < 3)
    {
        /*Hiện màn hình nhập sai password*/
        /*Chờ 1s*/
        if (esp_timer_is_active(system_control_timer_handler) == false)
        {
            ESP_ERROR_CHECK(esp_timer_start_periodic(system_control_timer_handler, 1000000));
        }
        device_mode.mode_update_state(&device_mode, LOGGING_IN, true);
        return;
    }
    device_mode.mode_update_state(&device_mode, LOCK_SYSTEM, true);
}

void system_common_lock_system(void)
{
    static int number_of_lock = 0;
    if (number_of_lock < 10)
    {
        /*Hiện màn hình khóa hệ thống*/
        if (esp_timer_is_active(system_control_timer_handler) == false)
        {
            ESP_ERROR_CHECK(esp_timer_start_periodic(system_control_timer_handler, 30000000));
        }
        device_mode.mode_tmp_lock = true;
        return;
    }
    device_mode.mode_lock_forever = true;
}

void system_control_task(void *pvParameter)
{
#if USED_FREERTOS
    while (1)
    {
        uint8_t current_mode_state = 0U;

        if (xSemaphoreTake(device_mode.mode_mutex, portMAX_DELAY) != pdTRUE)
        {
            ESP_LOGI(SYSTEM_CONTROL_TAG, "Error system mutex");
            return;
        }
        current_mode_state = device_mode.mode_state;
        xSemaphoreGive(device_mode.mode_mutex);

        switch (current_mode_state)
        {
            case USER_ENTERING_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User is entering password");
                if (esp_timer_is_active(system_control_timer_handler) == true)
                {
                    ESP_ERROR_CHECK(esp_timer_stop(system_control_timer_handler));
                }
                break;
            case USER_OPEN_LOCK:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User entered true password, opening door");
                system_user_open_lock_action();
                break;
            case USER_WRONG_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User entered wrong password, no opening door");
                system_common_wrong_password();
                break;
            case USER_LOCK_SYSTEM:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "User entered wrong password too much time, locking system");
                system_common_lock_system();
                break;

            case ADMIN_LOGGING_IN:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "Admin is entering password");
                if (esp_timer_is_active(system_control_timer_handler) == true)
                {
                    ESP_ERROR_CHECK(esp_timer_stop(system_control_timer_handler));
                }
                break;
            case ADMIN_LOGGED_IN:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "Admin entered true password, allow admin function");
                break;
            case ADMIN_WRONG_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "Admin entered wrong password, no allowing admin function");
                system_common_wrong_password();
                break;
            case ADMIN_LOCK_SYSTEM:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "Admin entered wrong password too much time , locking system");
                system_common_lock_system();
                break;
            case ADMIN_CHANGE_PASSWORD:
                ESP_LOGI(SYSTEM_CONTROL_TAG, "Admin is changing password");
                break;
            default:
                break;
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
#else
#endif
}
