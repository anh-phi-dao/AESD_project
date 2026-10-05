/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "door_sim.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_random.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define DOOR_SIM_TAG      "DOOR_SIM"
#define DOOR_SIM_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef struct st_door_sim_alert_info
{
    event_log_level_t level; /* Default level when the command does not give one. */
    const char       *p_msg;
} door_sim_alert_info_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void        door_sim_button_task(void *p_arg);
static app_err_t   door_sim_console_init(door_sim_instance_ctrl_t *const p_ctrl);
static int         door_sim_cmd(void *p_context, int argc, char **argv);
static app_err_t   door_sim_random_access(door_sim_instance_ctrl_t *const p_ctrl);
static app_err_t   door_sim_access(door_sim_instance_ctrl_t *const p_ctrl,
                                   event_log_method_t              method,
                                   event_log_result_t              result,
                                   const char *const               p_user);
static app_err_t   door_sim_alert(door_sim_instance_ctrl_t *const p_ctrl,
                                  event_log_alert_code_t          code,
                                  event_log_level_t               level);
static int         door_sim_find(const char *const p_name, const char *(*p_name_of)(int), int count);
static const char *door_sim_method_name(int value);
static const char *door_sim_level_name(int value);
static const char *door_sim_code_name(int value);
static void        door_sim_print_usage(void);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static const char *const s_users[] = {"Chủ nhà", "Người nhà", "Khách"};

static const door_sim_alert_info_t s_alert_info[EVENT_LOG_ALERT_COUNT] = {
    [EVENT_LOG_ALERT_PIN_BRUTEFORCE] = {EVENT_LOG_LEVEL_CRITICAL, "Sai PIN 5 lần liên tiếp (giả lập)"},
    [EVENT_LOG_ALERT_UNKNOWN_CARD]   = {EVENT_LOG_LEVEL_WARNING, "Quẹt thẻ lạ (giả lập)"},
    [EVENT_LOG_ALERT_DOOR_AJAR]      = {EVENT_LOG_LEVEL_WARNING, "Cửa mở quá 60 giây (giả lập)"},
    [EVENT_LOG_ALERT_TAMPER]         = {EVENT_LOG_LEVEL_CRITICAL, "Phát hiện cạy phá khóa (giả lập)"},
    [EVENT_LOG_ALERT_LOW_BATTERY]    = {EVENT_LOG_LEVEL_WARNING, "Pin còn 10% (giả lập)"},
    [EVENT_LOG_ALERT_POWER_LOST]     = {EVENT_LOG_LEVEL_WARNING, "Mất nguồn điện chính (giả lập)"},
};

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Starts the button task and registers the console command.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged for the lifetime of the instance.
 *
 * @retval APP_SUCCESS                  The simulator is running.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block is already initialized.
 * @retval APP_ERR_NO_MEMORY            A task could not be created.
 * @return                              The console error when the REPL could not start.
 **********************************************************************************************************************/
app_err_t door_sim_init(door_sim_instance_ctrl_t *const p_ctrl, const door_sim_config_t *const p_cfg)
{
#if DOOR_SIM_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);
    APP_ASSERT(NULL != p_cfg->p_log);
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    p_ctrl->p_cfg = p_cfg;
    p_ctrl->init  = true;

    if (p_cfg->button_gpio != GPIO_NUM_NC)
    {
        const gpio_config_t io = {
            .pin_bit_mask = 1ULL << p_cfg->button_gpio,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        if ((gpio_config(&io) != ESP_OK)
            || (xTaskCreate(door_sim_button_task,
                            "door_sim_btn",
                            DOOR_SIM_BUTTON_TASK_STACK,
                            p_ctrl,
                            DOOR_SIM_BUTTON_TASK_PRIO,
                            &p_ctrl->button_task)
                != pdPASS))
        {
            p_ctrl->init = false;
            return APP_ERR_NO_MEMORY;
        }
    }

    if (p_cfg->console)
    {
        const app_err_t ret = door_sim_console_init(p_ctrl);
        if (ret != APP_SUCCESS)
        {
            /* The button still works; a console clash should not take the whole simulator down. */
            ESP_LOGE(DOOR_SIM_TAG, "Console not available (application error %d)", ret);
        }
    }

    ESP_LOGW(DOOR_SIM_TAG,
             "Simulator active: short press on GPIO%d = access attempt, hold %u ms = tamper alert, "
             "type \"sim\" in the monitor for more",
             (int)p_cfg->button_gpio,
             (unsigned)DOOR_SIM_LONG_PRESS_MS);
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Polls the active-low button and acts on release, so the press length decides the action.
 **********************************************************************************************************************/
static void door_sim_button_task(void *p_arg)
{
    door_sim_instance_ctrl_t *p_ctrl     = (door_sim_instance_ctrl_t *)p_arg;
    const gpio_num_t          pin        = p_ctrl->p_cfg->button_gpio;
    bool                      pressed    = false;
    TickType_t                pressed_at = 0;

    while (1)
    {
        const bool level_low = (gpio_get_level(pin) == 0);
        if (level_low && !pressed)
        {
            pressed    = true;
            pressed_at = xTaskGetTickCount();
        }
        else if (!level_low && pressed)
        {
            pressed = false;
            if ((xTaskGetTickCount() - pressed_at) >= pdMS_TO_TICKS(DOOR_SIM_LONG_PRESS_MS))
            {
                (void)door_sim_alert(p_ctrl, EVENT_LOG_ALERT_TAMPER, s_alert_info[EVENT_LOG_ALERT_TAMPER].level);
            }
            else
            {
                (void)door_sim_random_access(p_ctrl);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DOOR_SIM_BUTTON_POLL_MS));
    }
}

/***********************************************************************************************************************
 * Starts a REPL on the configured console and registers "sim" and "help".
 **********************************************************************************************************************/
static app_err_t door_sim_console_init(door_sim_instance_ctrl_t *const p_ctrl)
{
    esp_console_repl_t       *p_repl      = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt                    = DOOR_SIM_PROMPT;
    repl_config.task_stack_size           = DOOR_SIM_CONSOLE_STACK;
    repl_config.task_priority             = DOOR_SIM_CONSOLE_PRIO;

#if defined(CONFIG_ESP_CONSOLE_UART_DEFAULT) || defined(CONFIG_ESP_CONSOLE_UART_CUSTOM)
    const esp_console_dev_uart_config_t hw_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t                           err       = esp_console_new_repl_uart(&hw_config, &repl_config, &p_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_CDC)
    const esp_console_dev_usb_cdc_config_t hw_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    esp_err_t                              err       = esp_console_new_repl_usb_cdc(&hw_config, &repl_config, &p_repl);
#elif defined(CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG)
    const esp_console_dev_usb_serial_jtag_config_t hw_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &p_repl);
#else
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;
#endif
    if (err != ESP_OK)
    {
        return (err == ESP_ERR_NO_MEM) ? APP_ERR_NO_MEMORY : APP_ERR_NOT_SUPPORTED;
    }

    const esp_console_cmd_t cmd = {
        .command        = "sim",
        .help           = "Simulate lock activity (dev board). Run \"sim\" alone for the subcommands.",
        .hint           = "<open|deny|alert|seed|clear> [args]",
        .func_w_context = door_sim_cmd,
        .context        = p_ctrl,
    };
    err = esp_console_cmd_register(&cmd);
    if (err == ESP_OK)
    {
        err = esp_console_register_help_command();
    }
    if (err == ESP_OK)
    {
        err = esp_console_start_repl(p_repl);
    }
    return (err == ESP_OK) ? APP_SUCCESS : APP_FAIL;
}

/***********************************************************************************************************************
 * "sim" console command. Returns 0 on success, 1 on a usage error (printed by the console as a non-zero code).
 **********************************************************************************************************************/
static int door_sim_cmd(void *p_context, int argc, char **argv)
{
    door_sim_instance_ctrl_t *p_ctrl = (door_sim_instance_ctrl_t *)p_context;
    if (argc < 2)
    {
        door_sim_print_usage();
        return 0;
    }

    const char *p_sub = argv[1];
    app_err_t   ret   = APP_SUCCESS;

    if ((strcmp(p_sub, "open") == 0) || (strcmp(p_sub, "deny") == 0))
    {
        const bool granted = (p_sub[0] == 'o');
        int        method  = (int)((esp_random() % 2U == 0U) ? EVENT_LOG_METHOD_PIN : EVENT_LOG_METHOD_NFC);
        if (argc >= 3)
        {
            method = door_sim_find(argv[2], door_sim_method_name, EVENT_LOG_METHOD_COUNT);
            if (method < 0)
            {
                printf("Unknown method \"%s\" (pin, nfc, button, key)\n", argv[2]);
                return 1;
            }
        }

        /* The rest of the line is the user name, so "sim open pin Nguyen Van A" works without quotes. Twice the
         * stored size, so event_log truncates it on a UTF-8 boundary instead of strncat cutting a character. */
        char user[EVENT_LOG_USER_SIZE * 2U] = "";
        if (argc >= 4)
        {
            for (int i = 3; i < argc; i++)
            {
                if (i > 3)
                {
                    (void)strncat(user, " ", sizeof(user) - strlen(user) - 1U);
                }
                (void)strncat(user, argv[i], sizeof(user) - strlen(user) - 1U);
            }
        }
        else if (granted && ((method == (int)EVENT_LOG_METHOD_PIN) || (method == (int)EVENT_LOG_METHOD_NFC)))
        {
            (void)snprintf(user, sizeof(user), "%s", s_users[esp_random() % DOOR_SIM_ARRAY(s_users)]);
        }
        ret = door_sim_access(
            p_ctrl, (event_log_method_t)method, granted ? EVENT_LOG_RESULT_GRANTED : EVENT_LOG_RESULT_DENIED, user);
    }
    else if (strcmp(p_sub, "alert") == 0)
    {
        int code = (int)EVENT_LOG_ALERT_TAMPER;
        if (argc >= 3)
        {
            code = door_sim_find(argv[2], door_sim_code_name, EVENT_LOG_ALERT_COUNT);
            if (code < 0)
            {
                printf(
                    "Unknown alert \"%s\" (pin_bruteforce, unknown_card, door_ajar, tamper, low_battery, "
                    "power_lost)\n",
                    argv[2]);
                return 1;
            }
        }
        int level = (int)s_alert_info[code].level;
        if (argc >= 4)
        {
            level = door_sim_find(argv[3], door_sim_level_name, EVENT_LOG_LEVEL_COUNT);
            if (level < 0)
            {
                printf("Unknown level \"%s\" (info, warning, critical)\n", argv[3]);
                return 1;
            }
        }
        ret = door_sim_alert(p_ctrl, (event_log_alert_code_t)code, (event_log_level_t)level);
    }
    else if (strcmp(p_sub, "seed") == 0)
    {
        long count = (argc >= 3) ? strtol(argv[2], NULL, 10) : (long)DOOR_SIM_SEED_DEFAULT;
        if ((count < 1) || (count > (long)DOOR_SIM_SEED_MAX))
        {
            printf("Count must be 1..%u\n", (unsigned)DOOR_SIM_SEED_MAX);
            return 1;
        }
        for (long i = 0; (i < count) && (ret == APP_SUCCESS); i++)
        {
            ret = door_sim_random_access(p_ctrl);
        }
    }
    else if (strcmp(p_sub, "clear") == 0)
    {
        ret = event_log_clear(p_ctrl->p_cfg->p_log);
        if (ret == APP_SUCCESS)
        {
            printf("Log cleared; web pages drop their copy when the new \"recent\" arrives.\n");
        }
    }
    else
    {
        door_sim_print_usage();
        return 1;
    }

    if (ret != APP_SUCCESS)
    {
        printf("Failed with application error %d\n", ret);
        return 1;
    }
    return 0;
}

/***********************************************************************************************************************
 * One access attempt with random method and outcome: mostly granted PIN/NFC with a known user.
 **********************************************************************************************************************/
static app_err_t door_sim_random_access(door_sim_instance_ctrl_t *const p_ctrl)
{
    const uint32_t     r       = esp_random();
    const bool         granted = (r % 5U) != 0U;
    event_log_method_t method  = ((r >> 4) % 2U == 0U) ? EVENT_LOG_METHOD_PIN : EVENT_LOG_METHOD_NFC;
    if (granted && (((r >> 8) % 8U) == 0U))
    {
        method = EVENT_LOG_METHOD_BUTTON;
    }
    const char *p_user = (granted && (method != EVENT_LOG_METHOD_BUTTON)) ? s_users[(r >> 12) % DOOR_SIM_ARRAY(s_users)]
                                                                          : NULL;
    return door_sim_access(p_ctrl, method, granted ? EVENT_LOG_RESULT_GRANTED : EVENT_LOG_RESULT_DENIED, p_user);
}

/***********************************************************************************************************************
 * Records one access attempt and logs it.
 **********************************************************************************************************************/
static app_err_t door_sim_access(door_sim_instance_ctrl_t *const p_ctrl,
                                 event_log_method_t              method,
                                 event_log_result_t              result,
                                 const char *const               p_user)
{
    event_log_event_t entry = {0};
    const app_err_t   ret   = event_log_add_event(p_ctrl->p_cfg->p_log, method, result, p_user, &entry);
    if (ret == APP_SUCCESS)
    {
        ESP_LOGI(DOOR_SIM_TAG,
                 "Event #%lu: %s %s%s%s",
                 (unsigned long)entry.id,
                 event_log_method_name(method),
                 event_log_result_name(result),
                 (entry.user[0] != '\0') ? " by " : "",
                 entry.user);
    }
    return ret;
}

/***********************************************************************************************************************
 * Records one alert with the default message for its code and logs it.
 **********************************************************************************************************************/
static app_err_t door_sim_alert(door_sim_instance_ctrl_t *const p_ctrl,
                                event_log_alert_code_t          code,
                                event_log_level_t               level)
{
    event_log_alert_t entry = {0};
    const app_err_t   ret   = event_log_add_alert(p_ctrl->p_cfg->p_log, level, code, s_alert_info[code].p_msg, &entry);
    if (ret == APP_SUCCESS)
    {
        ESP_LOGW(DOOR_SIM_TAG,
                 "Alert #%lu: %s (%s)",
                 (unsigned long)entry.id,
                 event_log_alert_code_name(code),
                 event_log_level_name(level));
    }
    return ret;
}

/***********************************************************************************************************************
 * Returns the enum value whose wire name equals p_name, or -1.
 **********************************************************************************************************************/
static int door_sim_find(const char *const p_name, const char *(*p_name_of)(int), int count)
{
    for (int i = 0; i < count; i++)
    {
        if (strcmp(p_name, p_name_of(i)) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* Adapters so door_sim_find() can walk any of the name tables. */
static const char *door_sim_method_name(int value)
{
    return event_log_method_name((event_log_method_t)value);
}

static const char *door_sim_level_name(int value)
{
    return event_log_level_name((event_log_level_t)value);
}

static const char *door_sim_code_name(int value)
{
    return event_log_alert_code_name((event_log_alert_code_t)value);
}

/***********************************************************************************************************************
 * Prints the subcommands.
 **********************************************************************************************************************/
static void door_sim_print_usage(void)
{
    /* linenoise drops every non-ASCII byte, so names typed here lose their Vietnamese diacritics. */
    printf(
        "sim open  [pin|nfc|button|key] [user]   access granted (type the name without diacritics)\n"
        "sim deny  [pin|nfc|button|key]          access denied\n"
        "sim alert [code] [info|warning|critical] alert, default tamper\n"
        "          codes: pin_bruteforce unknown_card door_ajar tamper low_battery power_lost\n"
        "sim seed  [1..%u]                        random access attempts\n"
        "sim clear                                drop the whole log (web clears too)\n",
        (unsigned)DOOR_SIM_SEED_MAX);
}
