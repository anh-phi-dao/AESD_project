#include "system_task.h"

#include "esp_heap_caps.h"
#include "event_log.h"
#include "fingerprint_task.h"
#include "network_task.h"
#include "nvs.h"
#include "solenoid_lock.h"
#include "voice_task.h"

#define SYSTEM_NVS_NAMESPACE "system"
#define SYSTEM_NVS_KEY_LOCKS "locks_forever"

static void    system_on_access(system_method_t method, bool granted, uint8_t user);
static void    system_task_log_stacks(void);
static void    system_on_alert(system_alert_t alert, system_lock_target_t target, uint32_t lock_s);
static void    system_on_locks_forever(uint8_t mask);
static uint8_t system_load_locks_forever(void);
static void    system_on_door(bool unlocked);
static void system_on_voice(system_voice_t action, bool owner);

static const system_control_hooks_t s_hooks = {
    .on_access         = system_on_access,
    .on_alert          = system_on_alert,
    .on_locks_forever  = system_on_locks_forever,
    .on_door           = system_on_door,
    .on_voice          = system_on_voice,
    .on_play           = voice_task_play,
    .on_play_stop      = voice_task_play_stop,
    .on_message_delete = voice_task_delete_owner,
    .on_finger_delete  = fingerprint_task_request_delete,
};

void boot_system_control(void)
{
    app_err_t ret = system_control_init(&s_hooks);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(SYSTEM_CONTROL_TAG, "system_control_init failed with application error: %d", ret);
        while (1)
        {
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }
    /* A method locked until an admin login stays locked across a restart: pulling the power is no way around it. */
    system_control_set_locks_forever(system_load_locks_forever());
    ESP_LOGI(SYSTEM_CONTROL_TAG, "System control initialized successfully");
}

void system_control_task(void *pvParameter)
{
    (void)pvParameter;
#if USED_FREERTOS
    uint32_t ticks = 0U;
    while (1)
    {
        system_control_tick();
        if (0U == (++ticks % SYSTEM_RAM_LOG_TICKS))
        {
            /* No PSRAM: the lowest point shows how close TLS, voice uploads and Wi-Fi came to running out. */
            ESP_LOGI(SYSTEM_CONTROL_TAG,
                     "RAM free %u, lowest since boot %u, largest block %u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
            system_task_log_stacks();
        }
        vTaskDelay(pdMS_TO_TICKS(SYSTEM_CONTROL_TICK_MS));
    }
#endif
}

/* Unused stack of the busiest tasks, in bytes: near 0 means the next deeper call overflows. */
static void system_task_log_stacks(void)
{
    static const char *const s_names[] = {"ui_flow", "lvgl", "voice", "keypad", "rfid_task", "fingerprint", "storage"};

    for (size_t i = 0U; i < (sizeof(s_names) / sizeof(s_names[0])); i++)
    {
        const TaskHandle_t task = xTaskGetHandle(s_names[i]);
        if (NULL != task)
        {
            ESP_LOGI(
                SYSTEM_CONTROL_TAG, "Stack left %-11s %u", s_names[i], (unsigned)uxTaskGetStackHighWaterMark(task));
        }
    }
}

/* Access attempts for the history on the web page. */
static void system_on_access(system_method_t method, bool granted, uint8_t user)
{
    static const event_log_method_t s_methods[]
        = {EVENT_LOG_METHOD_PIN, EVENT_LOG_METHOD_NFC, EVENT_LOG_METHOD_FINGERPRINT};
    event_log_instance_ctrl_t *const p_log                     = network_task_get_event_log();
    char                             name[EVENT_LOG_USER_SIZE] = "";

    if (NULL == p_log)
    {
        return;
    }
    if (0U != user)
    {
        (void)snprintf(name, sizeof(name), "%s", system_control_user_name(user));
    }

    const app_err_t ret = event_log_add_event(
        p_log, s_methods[method], granted ? EVENT_LOG_RESULT_GRANTED : EVENT_LOG_RESULT_DENIED, name, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "event_log_add_event failed with application error: %d", ret);
    }
}

static void system_on_alert(system_alert_t alert, system_lock_target_t target, uint32_t lock_s)
{
    static const char *const s_targets[SYSTEM_LOCK_COUNT] = {"PIN", "card", "fingerprint", "admin login"};
    event_log_instance_ctrl_t *const p_log = network_task_get_event_log();
    event_log_alert_code_t           code  = EVENT_LOG_ALERT_PIN_BRUTEFORCE;
    event_log_level_t                level = EVENT_LOG_LEVEL_WARNING;
    char                             msg[EVENT_LOG_ALERT_MSG_SIZE];

    if (NULL == p_log)
    {
        return;
    }
    if (SYSTEM_ALERT_UNKNOWN_CARD == alert)
    {
        code = EVENT_LOG_ALERT_UNKNOWN_CARD;
        (void)snprintf(msg, sizeof(msg), "Unknown card at the reader");
    }
    else if (SYSTEM_LOCK_FOREVER == lock_s)
    {
        level = EVENT_LOG_LEVEL_CRITICAL;
        (void)snprintf(msg,
                       sizeof(msg),
                       "Too many wrong tries: %s locked until an admin login",
                       s_targets[(target < SYSTEM_LOCK_COUNT) ? target : SYSTEM_LOCK_PIN]);
    }
    else
    {
        (void)snprintf(msg,
                       sizeof(msg),
                       "Too many wrong tries: %s locked for %lu s, the other methods still work",
                       s_targets[(target < SYSTEM_LOCK_COUNT) ? target : SYSTEM_LOCK_PIN],
                       (unsigned long)lock_s);
    }

    const app_err_t ret = event_log_add_alert(p_log, level, code, msg, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "event_log_add_alert failed with application error: %d", ret);
    }
}

/* Bit n: system_lock_target_t n locked until an admin login. Written only when it changes, so rarely. */
static void system_on_locks_forever(uint8_t mask)
{
    nvs_handle_t handle;

    if (ESP_OK != nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READWRITE, &handle))
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "Cannot open NVS to keep the lockouts");
        return;
    }
    if ((ESP_OK != nvs_set_u8(handle, SYSTEM_NVS_KEY_LOCKS, mask)) || (ESP_OK != nvs_commit(handle)))
    {
        ESP_LOGW(SYSTEM_CONTROL_TAG, "Saving the lockouts failed");
    }
    nvs_close(handle);
}

static uint8_t system_load_locks_forever(void)
{
    nvs_handle_t handle;
    uint8_t      mask = 0U;

    if (ESP_OK == nvs_open(SYSTEM_NVS_NAMESPACE, NVS_READONLY, &handle))
    {
        if (ESP_OK != nvs_get_u8(handle, SYSTEM_NVS_KEY_LOCKS, &mask))
        {
            mask = 0U;
        }
        nvs_close(handle);
    }
    return mask;
}

static void system_on_door(bool unlocked)
{
    if (!unlocked)
    {
        solenoid_lock_lock();
    }
    else if (APP_SUCCESS != solenoid_lock_unlock())
    {
        ESP_LOGE(SYSTEM_CONTROL_TAG, "Solenoid unlock failed, door stays locked");
    }
    ESP_LOGI(SYSTEM_CONTROL_TAG, "Door %s", unlocked ? "unlocked" : "locked");
}

static void system_on_voice(system_voice_t action, bool owner)
{
    switch (action)
    {
        case SYSTEM_VOICE_START:
            voice_task_record_start(owner);
            break;
        case SYSTEM_VOICE_SEND:
            voice_task_record_send();
            break;
        default:
            voice_task_record_stop(SYSTEM_VOICE_STOP == action);
            break;
    }
}
