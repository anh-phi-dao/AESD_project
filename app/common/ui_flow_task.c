/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "ui_flow_task.h"

#if UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "event_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "keypad.h"
#include "network_task.h"
#include "ui.h" /* SquareLine objects (ui_*), exported by app/ui_service. */
#include "ui_service.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_FLOW_TAG          "UI_FLOW"
#define UI_FLOW_QUEUE_LEN    16U
#define UI_FLOW_KEY_COUNT    (KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT)
#define UI_FLOW_TEXT_SIZE    9U   /* The SquareLine text areas take at most 8 digits. */
#define UI_FLOW_BAR_WIDTH    240U /* Width of ui_VoiceTrack and ui_StatusTrack. */
#define UI_FLOW_WIFI_POLL_MS 1000U
#define UI_FLOW_DETAIL_SIZE  32U
#define UI_FLOW_TOPBAR_USER  "SMART LOCK"
#define UI_FLOW_TOPBAR_ADMIN "ADMIN MODE"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Where the user is. Several states share a screen: Input serves PIN entry and the admin steps, Status serves both
 * lockouts and scanning, Menu serves the admin menu and the PIN / fingerprint / card menus. */
typedef enum e_ui_flow_state
{
    UI_FLOW_STATE_HOME = 0,
    UI_FLOW_STATE_PIN_ENTRY,     /* Input: ENTER PIN. */
    UI_FLOW_STATE_LOCKOUT,       /* Status: too many wrong PINs. */
    UI_FLOW_STATE_VOICE,         /* Voice: recording. */
    UI_FLOW_STATE_ADMIN_LOGIN,   /* AdminLogin. */
    UI_FLOW_STATE_ADMIN_LOCKOUT, /* Status: too many wrong admin logins. */
    UI_FLOW_STATE_ADMIN_MENU,    /* Menu: ADMIN MENU. */
    UI_FLOW_STATE_CRED_MENU,     /* Menu: PIN CODES / FINGERPRINTS / RFID CARDS. */
    UI_FLOW_STATE_ADMIN_INPUT,   /* Input: user ID, new PIN, PIN again. */
    UI_FLOW_STATE_ADMIN_SCAN,    /* Status: waiting for a finger or a card to enroll. */
} ui_flow_state_t;

/* Credential type an admin sub menu manages; also its row number in ADMIN MENU minus one. */
typedef enum e_ui_flow_kind
{
    UI_FLOW_KIND_PIN = 0,
    UI_FLOW_KIND_FINGER,
    UI_FLOW_KIND_CARD,
    UI_FLOW_KIND_COUNT
} ui_flow_kind_t;

typedef enum e_ui_flow_step
{
    UI_FLOW_STEP_ADD_ID = 0,
    UI_FLOW_STEP_ADD_PIN,
    UI_FLOW_STEP_ADD_CONFIRM,
    UI_FLOW_STEP_DELETE_ID,
} ui_flow_step_t;

typedef enum e_ui_flow_msg_type
{
    UI_FLOW_MSG_KEY_DOWN = 0,
    UI_FLOW_MSG_KEY_UP,
    UI_FLOW_MSG_SUBMIT, /* # on a text area, from ui_service in the LVGL task. */
} ui_flow_msg_type_t;

typedef struct st_ui_flow_msg
{
    ui_flow_msg_type_t  type;
    uint8_t             key_index; /* KEY_DOWN / KEY_UP: index into s_keypad_keymap. */
    ui_service_screen_t screen;    /* SUBMIT: screen the text came from. */
    char                first[UI_FLOW_TEXT_SIZE];
    char                second[UI_FLOW_TEXT_SIZE];
} ui_flow_msg_t;

typedef struct st_ui_flow_key
{
    TickType_t      down_tick;
    ui_flow_state_t down_state; /* A hold acts on the state the key went down in. */
    bool            down;
    bool            hold_done; /* One hold action per press. */
} ui_flow_key_t;

/* Used only by the flow task, except queue (any task) and p_ui (read-only after boot). */
typedef struct st_ui_flow_ctx
{
    ui_service_instance_ctrl_t *p_ui;
    QueueHandle_t               queue;
    ui_flow_state_t             state;
    ui_flow_kind_t              kind;
    ui_flow_step_t              step;
    uint8_t                     pending_user;                   /* Admin add / delete: chosen user ID. */
    char                        pending_pin[UI_FLOW_TEXT_SIZE]; /* Admin add: first entry of the new PIN. */
    TickType_t                  state_tick;                     /* Entry into the state: countdowns. */
    TickType_t                  activity_tick;                  /* Last key: idle timeouts. */
    TickType_t                  unlock_tick;
    TickType_t                  wifi_tick;
    uint32_t                    shown_seconds; /* Value a countdown last drew; it redraws once per second. */
    uint8_t                     pin_fails;
    uint8_t                     admin_fails;
    int8_t                      wifi_shown; /* -1 not drawn yet, 0 off, 1 on. */
    bool                        unlocked;
    ui_flow_key_t               keys[UI_FLOW_KEY_COUNT];
} ui_flow_ctx_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void ui_flow_task(void *pvParameter);
static void ui_flow_submit_cb(ui_service_screen_t screen, const char *p_first, const char *p_second, void *p_context);
static void ui_flow_handle_key_down(uint8_t key_index, TickType_t now);
static void ui_flow_handle_hold(char key, ui_flow_state_t down_state);
static void ui_flow_handle_submit(const ui_flow_msg_t *const p_msg);
static void ui_flow_tick(TickType_t now);
static void ui_flow_update_timed(TickType_t now);
static void ui_flow_check_pin(const char *p_pin);
static void ui_flow_check_admin(const char *p_id, const char *p_password);
static void ui_flow_admin_input(const char *p_text);
static void ui_flow_go_home(void);
static void ui_flow_go_pin_entry(void);
static void ui_flow_go_lockout(bool admin);
static void ui_flow_go_voice(void);
static void ui_flow_voice_end(bool cancelled);
static void ui_flow_go_admin_login(void);
static void ui_flow_go_admin_menu(void);
static void ui_flow_go_cred_menu(void);
static void ui_flow_go_admin_input(ui_flow_step_t step);
static void ui_flow_go_scan(void);
static void ui_flow_exit_admin(const char *p_title, const char *p_detail);
static void ui_flow_enter(ui_flow_state_t state);
static bool ui_flow_lock(void);
static void ui_flow_unlock(void);
static void ui_flow_show(ui_service_screen_t screen);
static void ui_flow_popup(ui_service_popup_level_t level, const char *p_title, const char *p_detail);
static void ui_flow_popup_tries(ui_service_popup_level_t level, const char *p_title, uint8_t tries_left);
static void ui_flow_menu_set(
    const char *p_title, const char *p_r1, const char *p_r2, const char *p_r3, const char *p_r4);
static void     ui_flow_bar_set(lv_obj_t *p_fill, uint32_t value, uint32_t max);
static uint32_t ui_flow_elapsed_ms(TickType_t since, TickType_t now);
static bool     ui_flow_text_equal(const char *p_a, const char *p_b);
static uint8_t  ui_flow_find_pin(const char *p_pin);
static uint8_t  ui_flow_parse_user(const char *p_text);
static bool     ui_flow_user_has(ui_flow_kind_t kind, uint8_t user);
static uint8_t  ui_flow_user_count(ui_flow_kind_t kind);
static void     ui_flow_log_event(event_log_result_t result, uint8_t user);
static void     ui_flow_log_alert(const char *p_msg);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static ui_flow_ctx_t s_flow       = {0};
static bool          s_flow_ready = false;

/* Credentials, indexed by user ID - 1. RAM only until storage_manager exists (see UI_FLOW_DEFAULT_PIN). */
static char s_pins[UI_FLOW_USER_COUNT][UI_FLOW_TEXT_SIZE];
static bool s_enrolled[UI_FLOW_KIND_COUNT][UI_FLOW_USER_COUNT]; /* FINGER and CARD; PIN uses s_pins. */

static const char *const s_kind_menu_titles[UI_FLOW_KIND_COUNT]   = {"PIN CODES", "FINGERPRINTS", "RFID CARDS"};
static const char *const s_kind_delete_titles[UI_FLOW_KIND_COUNT] = {"DELETE PIN", "DELETE FINGERPRINT", "DELETE CARD"};

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

void boot_ui_flow(void)
{
    ui_service_instance_ctrl_t *const p_ui = ui_task_get_ui();
    if (NULL == p_ui)
    {
        ESP_LOGW(UI_FLOW_TAG, "No screen, screen flow not started");
        return;
    }

    s_flow.p_ui       = p_ui;
    s_flow.state      = UI_FLOW_STATE_HOME;
    s_flow.wifi_shown = -1;
    s_flow.queue      = xQueueCreate(UI_FLOW_QUEUE_LEN, sizeof(ui_flow_msg_t));
    if (NULL == s_flow.queue)
    {
        ESP_LOGE(UI_FLOW_TAG, "No memory for the screen flow queue");
        return;
    }
    (void)snprintf(s_pins[0], sizeof(s_pins[0]), "%s", UI_FLOW_DEFAULT_PIN);

    app_err_t ret = ui_service_set_submit_cb(p_ui, ui_flow_submit_cb, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(UI_FLOW_TAG, "ui_service_set_submit_cb failed with application error: %d", ret);
        return;
    }

    if (pdPASS != xTaskCreate(ui_flow_task, "ui_flow", UI_FLOW_TASK_STACK, NULL, UI_FLOW_TASK_PRIORITY, NULL))
    {
        ESP_LOGE(UI_FLOW_TAG, "Failed to create the screen flow task");
        (void)ui_service_set_submit_cb(p_ui, NULL, NULL);
        return;
    }
    s_flow_ready = true;
}

void ui_flow_post_keys(uint16_t pressed_keys, uint16_t previous_keys)
{
    if (!s_flow_ready)
    {
        return;
    }

    const uint16_t changed = pressed_keys ^ previous_keys;

    for (uint8_t key_index = 0U; key_index < UI_FLOW_KEY_COUNT; key_index++)
    {
        const uint16_t bit = (uint16_t)(1U << key_index);
        if ((changed & bit) != 0U)
        {
            const ui_flow_msg_t msg = {
                .type      = ((pressed_keys & bit) != 0U) ? UI_FLOW_MSG_KEY_DOWN : UI_FLOW_MSG_KEY_UP,
                .key_index = key_index,
            };
            if (pdTRUE != xQueueSend(s_flow.queue, &msg, 0))
            {
                ESP_LOGW(UI_FLOW_TAG, "Queue full, key dropped");
            }
        }
    }
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Handles keys and submits as they come, and every UI_FLOW_TICK_MS the holds, timeouts and countdowns.
 **********************************************************************************************************************/
static void ui_flow_task(void *pvParameter)
{
    (void)pvParameter;
    ui_flow_msg_t msg;

    ui_flow_go_home();

    while (1)
    {
        if (pdTRUE == xQueueReceive(s_flow.queue, &msg, pdMS_TO_TICKS(UI_FLOW_TICK_MS)))
        {
            switch (msg.type)
            {
                case UI_FLOW_MSG_KEY_DOWN:
                    ui_flow_handle_key_down(msg.key_index, xTaskGetTickCount());
                    break;
                case UI_FLOW_MSG_KEY_UP:
                    s_flow.keys[msg.key_index].down = false;
                    break;
                case UI_FLOW_MSG_SUBMIT:
                    ui_flow_handle_submit(&msg);
                    memset(&msg, 0, sizeof(msg)); /* Do not keep PINs around. */
                    break;
                default:
                    break;
            }
        }
        ui_flow_tick(xTaskGetTickCount());
    }
}

/***********************************************************************************************************************
 * # on Input or AdminLogin. Runs in the LVGL task holding its lock: copy the texts and hand them to the flow task.
 **********************************************************************************************************************/
static void ui_flow_submit_cb(ui_service_screen_t screen, const char *p_first, const char *p_second, void *p_context)
{
    (void)p_context;
    ui_flow_msg_t msg = {
        .type   = UI_FLOW_MSG_SUBMIT,
        .screen = screen,
    };

    (void)snprintf(msg.first, sizeof(msg.first), "%s", p_first);
    (void)snprintf(msg.second, sizeof(msg.second), "%s", (NULL != p_second) ? p_second : "");
    if (pdTRUE != xQueueSend(s_flow.queue, &msg, 0))
    {
        ESP_LOGW(UI_FLOW_TAG, "Queue full, entry dropped");
    }
}

/***********************************************************************************************************************
 * Key presses that act at once. Digits and * on Input / AdminLogin are typed by LVGL itself (ui_task_post_new_keys());
 * here they only keep the screen from timing out.
 **********************************************************************************************************************/
static void ui_flow_handle_key_down(uint8_t key_index, TickType_t now)
{
    const char           key   = s_keypad_keymap[key_index];
    ui_flow_key_t *const p_key = &s_flow.keys[key_index];

    p_key->down          = true;
    p_key->hold_done     = false;
    p_key->down_tick     = now;
    p_key->down_state    = s_flow.state;
    s_flow.activity_tick = now;

    switch (s_flow.state)
    {
        case UI_FLOW_STATE_HOME:
            /* The digit itself already went to the hidden ui_InputField, which Input keeps. */
            if ((key >= '0') && (key <= '9'))
            {
                ui_flow_go_pin_entry();
            }
            break;

        case UI_FLOW_STATE_VOICE:
            if ('#' == key)
            {
                ui_flow_voice_end(false);
            }
            break;

        case UI_FLOW_STATE_ADMIN_MENU:
            if ((key >= '1') && (key <= '3'))
            {
                s_flow.kind = (ui_flow_kind_t)(key - '1');
                ui_flow_go_cred_menu();
            }
            else if ('0' == key)
            {
                ESP_LOGI(UI_FLOW_TAG, "Admin logout");
                ui_flow_exit_admin(NULL, NULL);
            }
            break;

        case UI_FLOW_STATE_CRED_MENU:
            if ('1' == key)
            {
                if (UI_FLOW_KIND_PIN == s_flow.kind)
                {
                    ui_flow_go_admin_input(UI_FLOW_STEP_ADD_ID);
                }
                else
                {
                    ui_flow_go_scan();
                }
            }
            else if ('2' == key)
            {
                ui_flow_go_admin_input(UI_FLOW_STEP_DELETE_ID);
            }
            else if ('0' == key)
            {
                ui_flow_go_admin_menu();
            }
            break;

        default:
            break;
    }
}

/***********************************************************************************************************************
 * A key held UI_FLOW_HOLD_MS. Only the state the key went down in counts, so a hold that opened a screen does not also
 * act on it. A 0 held on Home has already opened Input (its first press is a PIN digit).
 **********************************************************************************************************************/
static void ui_flow_handle_hold(char key, ui_flow_state_t down_state)
{
    switch (down_state)
    {
        case UI_FLOW_STATE_HOME:
            if ((UI_FLOW_STATE_HOME == s_flow.state) && ('*' == key))
            {
                ui_flow_go_voice();
            }
            else if ((UI_FLOW_STATE_HOME == s_flow.state) && ('#' == key))
            {
                /* TODO(audio_service): play the newest message. None can be received yet. */
                ui_flow_popup(UI_SERVICE_POPUP_INFO, "No new messages", NULL);
            }
            else if ((UI_FLOW_STATE_PIN_ENTRY == s_flow.state) && ('0' == key))
            {
                ui_flow_go_admin_login();
            }
            break;

        case UI_FLOW_STATE_PIN_ENTRY:
        case UI_FLOW_STATE_ADMIN_LOGIN:
            if ((down_state == s_flow.state) && ('*' == key))
            {
                ui_flow_go_home();
            }
            break;

        case UI_FLOW_STATE_VOICE:
            if ((UI_FLOW_STATE_VOICE == s_flow.state) && ('*' == key))
            {
                ui_flow_voice_end(true);
            }
            break;

        case UI_FLOW_STATE_ADMIN_INPUT:
        case UI_FLOW_STATE_ADMIN_SCAN:
            if ((down_state == s_flow.state) && ('*' == key))
            {
                memset(s_flow.pending_pin, 0, sizeof(s_flow.pending_pin));
                ui_flow_go_cred_menu();
            }
            break;

        default:
            break;
    }
}

/***********************************************************************************************************************
 * # on a text area. An entry from a screen the flow has already left (keys typed while it changed) is dropped.
 **********************************************************************************************************************/
static void ui_flow_handle_submit(const ui_flow_msg_t *const p_msg)
{
    s_flow.activity_tick = xTaskGetTickCount();

    if ((UI_FLOW_STATE_PIN_ENTRY == s_flow.state) && (UI_SERVICE_SCREEN_INPUT == p_msg->screen))
    {
        ui_flow_check_pin(p_msg->first);
    }
    else if ((UI_FLOW_STATE_ADMIN_LOGIN == s_flow.state) && (UI_SERVICE_SCREEN_ADMIN_LOGIN == p_msg->screen))
    {
        ui_flow_check_admin(p_msg->first, p_msg->second);
    }
    else if ((UI_FLOW_STATE_ADMIN_INPUT == s_flow.state) && (UI_SERVICE_SCREEN_INPUT == p_msg->screen))
    {
        ui_flow_admin_input(p_msg->first);
    }
}

/***********************************************************************************************************************
 * Holds, idle timeouts, countdowns, the UNLOCKED label and the Wi-Fi mark in the top bar.
 **********************************************************************************************************************/
static void ui_flow_tick(TickType_t now)
{
    for (uint8_t key_index = 0U; key_index < UI_FLOW_KEY_COUNT; key_index++)
    {
        ui_flow_key_t *const p_key = &s_flow.keys[key_index];
        if (p_key->down && !p_key->hold_done && (ui_flow_elapsed_ms(p_key->down_tick, now) >= UI_FLOW_HOLD_MS))
        {
            p_key->hold_done = true;
            ui_flow_handle_hold(s_keypad_keymap[key_index], p_key->down_state);
        }
    }

    const uint32_t idle_ms = ui_flow_elapsed_ms(s_flow.activity_tick, now);
    switch (s_flow.state)
    {
        case UI_FLOW_STATE_PIN_ENTRY:
            if (idle_ms >= UI_FLOW_PIN_IDLE_MS)
            {
                ui_flow_go_home();
            }
            break;
        case UI_FLOW_STATE_ADMIN_LOGIN:
            if (idle_ms >= UI_FLOW_LOGIN_IDLE_MS)
            {
                ui_flow_go_home();
            }
            break;
        case UI_FLOW_STATE_ADMIN_MENU:
        case UI_FLOW_STATE_CRED_MENU:
        case UI_FLOW_STATE_ADMIN_INPUT:
            if (idle_ms >= UI_FLOW_ADMIN_IDLE_MS)
            {
                ESP_LOGI(UI_FLOW_TAG, "Admin mode timed out");
                ui_flow_exit_admin("Admin mode ended", "No key pressed");
            }
            break;
        default:
            ui_flow_update_timed(now);
            break;
    }

    /* TODO(lock_service): relock the solenoid here; until then only the label changes. */
    if (s_flow.unlocked && (ui_flow_elapsed_ms(s_flow.unlock_tick, now) >= UI_FLOW_UNLOCK_SHOW_MS))
    {
        s_flow.unlocked = false;
        if (ui_flow_lock())
        {
            lv_label_set_text(ui_HomeState, "LOCKED");
            ui_flow_unlock();
        }
    }

    if ((s_flow.wifi_shown < 0) || (ui_flow_elapsed_ms(s_flow.wifi_tick, now) >= UI_FLOW_WIFI_POLL_MS))
    {
        s_flow.wifi_tick       = now;
        const int8_t connected = network_task_is_wifi_connected() ? 1 : 0;
        if ((connected != s_flow.wifi_shown) && ui_flow_lock())
        {
            lv_label_set_text(ui_TopBarWifiOff, (0 != connected) ? "" : LV_SYMBOL_CLOSE);
            ui_flow_unlock();
            s_flow.wifi_shown = connected;
        }
    }
}

/***********************************************************************************************************************
 * States that end on their own: lockouts, recording, scanning. Redraws the value and bar once per second and leaves
 * the state when its time is up.
 **********************************************************************************************************************/
static void ui_flow_update_timed(TickType_t now)
{
    const uint32_t elapsed_ms = ui_flow_elapsed_ms(s_flow.state_tick, now);
    uint32_t       total_ms;

    switch (s_flow.state)
    {
        case UI_FLOW_STATE_LOCKOUT:
        case UI_FLOW_STATE_ADMIN_LOCKOUT:
            total_ms = UI_FLOW_LOCKOUT_S * 1000U;
            break;
        case UI_FLOW_STATE_VOICE:
            total_ms = UI_FLOW_VOICE_MAX_S * 1000U;
            break;
        case UI_FLOW_STATE_ADMIN_SCAN:
            total_ms = UI_FLOW_SCAN_TIMEOUT_S * 1000U;
            break;
        default:
            return;
    }

    if (elapsed_ms >= total_ms)
    {
        switch (s_flow.state)
        {
            case UI_FLOW_STATE_LOCKOUT:
                s_flow.pin_fails = 0U;
                ui_flow_go_home();
                break;
            case UI_FLOW_STATE_ADMIN_LOCKOUT:
                s_flow.admin_fails = 0U;
                ui_flow_go_home();
                break;
            case UI_FLOW_STATE_VOICE:
                ui_flow_voice_end(false);
                break;
            default: /* UI_FLOW_STATE_ADMIN_SCAN */
                /* TODO(pn532 / fingerprint driver): enroll here and mark s_enrolled. No reader is connected yet. */
                ui_flow_go_cred_menu();
                ui_flow_popup(UI_SERVICE_POPUP_ERROR,
                              (UI_FLOW_KIND_FINGER == s_flow.kind) ? "No finger detected" : "No card detected",
                              "Reader not connected yet");
                break;
        }
        return;
    }

    const bool     voice     = (UI_FLOW_STATE_VOICE == s_flow.state);
    const uint32_t remain_ms = total_ms - elapsed_ms;
    const uint32_t seconds   = voice ? (elapsed_ms / 1000U) : ((remain_ms + 999U) / 1000U);

    if ((seconds == s_flow.shown_seconds) || !ui_flow_lock())
    {
        return;
    }
    s_flow.shown_seconds = seconds;

    switch (s_flow.state)
    {
        case UI_FLOW_STATE_VOICE:
            lv_label_set_text_fmt(ui_VoiceTime, "00:%02u / 00:%02u", (unsigned)seconds, (unsigned)UI_FLOW_VOICE_MAX_S);
            ui_flow_bar_set(ui_VoiceFill, elapsed_ms, total_ms);
            break;
        case UI_FLOW_STATE_ADMIN_SCAN:
            lv_label_set_text_fmt(ui_StatusValue, "Waiting... %u s", (unsigned)seconds);
            ui_flow_bar_set(ui_StatusFill, remain_ms, total_ms);
            break;
        default:
            lv_label_set_text_fmt(ui_StatusValue,
                                  "%s locked for %u s",
                                  (UI_FLOW_STATE_LOCKOUT == s_flow.state) ? "Door" : "Admin",
                                  (unsigned)seconds);
            ui_flow_bar_set(ui_StatusFill, remain_ms, total_ms);
            break;
    }
    ui_flow_unlock();
}

/***********************************************************************************************************************
 * # on ENTER PIN. Checked here, locally; nothing about a PIN goes over the network except granted / denied.
 **********************************************************************************************************************/
static void ui_flow_check_pin(const char *p_pin)
{
    const uint8_t user = ui_flow_find_pin(p_pin);

    if (0U != user)
    {
        ESP_LOGI(UI_FLOW_TAG, "PIN accepted, user %u", user);
        ui_flow_log_event(EVENT_LOG_RESULT_GRANTED, user);
        s_flow.pin_fails   = 0U;
        s_flow.unlocked    = true;
        s_flow.unlock_tick = xTaskGetTickCount();
        /* TODO(lock_service): open the solenoid here. */
        ui_flow_go_home();
        ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Welcome", "Door unlocked");
        return;
    }

    s_flow.pin_fails++;
    ESP_LOGW(UI_FLOW_TAG, "Wrong PIN (%u / %u)", s_flow.pin_fails, UI_FLOW_PIN_MAX_TRIES);
    ui_flow_log_event(EVENT_LOG_RESULT_DENIED, 0U);

    if (s_flow.pin_fails >= UI_FLOW_PIN_MAX_TRIES)
    {
        ui_flow_log_alert("Too many wrong PINs, keypad locked");
        ui_flow_go_lockout(false);
    }
    else
    {
        ui_flow_show(UI_SERVICE_SCREEN_INPUT); /* Empties the field. */
        ui_flow_popup_tries(
            UI_SERVICE_POPUP_ERROR, "Authentication failed", (uint8_t)(UI_FLOW_PIN_MAX_TRIES - s_flow.pin_fails));
    }
}

/***********************************************************************************************************************
 * # on the admin password.
 **********************************************************************************************************************/
static void ui_flow_check_admin(const char *p_id, const char *p_password)
{
    /* & rather than &&: both are compared, so the time taken does not tell which one was wrong. */
    if (ui_flow_text_equal(p_id, UI_FLOW_ADMIN_ID) & ui_flow_text_equal(p_password, UI_FLOW_ADMIN_PASSWORD))
    {
        ESP_LOGI(UI_FLOW_TAG, "Admin login");
        s_flow.admin_fails = 0U;
        ui_flow_go_admin_menu();
        return;
    }

    s_flow.admin_fails++;
    ESP_LOGW(UI_FLOW_TAG, "Wrong admin login (%u / %u)", s_flow.admin_fails, UI_FLOW_ADMIN_MAX_TRIES);

    if (s_flow.admin_fails >= UI_FLOW_ADMIN_MAX_TRIES)
    {
        ui_flow_log_alert("Too many wrong admin logins, admin locked");
        ui_flow_go_lockout(true);
    }
    else
    {
        ui_flow_show(UI_SERVICE_SCREEN_ADMIN_LOGIN); /* Empties both fields, back to the ID. */
        ui_flow_popup_tries(
            UI_SERVICE_POPUP_ERROR, "Wrong ID or password", (uint8_t)(UI_FLOW_ADMIN_MAX_TRIES - s_flow.admin_fails));
    }
}

/***********************************************************************************************************************
 * # on an admin Input step: user ID to add or delete, new PIN, PIN again.
 **********************************************************************************************************************/
static void ui_flow_admin_input(const char *p_text)
{
    char detail[UI_FLOW_DETAIL_SIZE];

    if ((UI_FLOW_STEP_ADD_ID == s_flow.step) || (UI_FLOW_STEP_DELETE_ID == s_flow.step))
    {
        const uint8_t user = ui_flow_parse_user(p_text);
        if (0U == user)
        {
            (void)snprintf(detail, sizeof(detail), "Use 1 - %u", UI_FLOW_USER_COUNT);
            ui_flow_show(UI_SERVICE_SCREEN_INPUT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Invalid user ID", detail);
            return;
        }

        const bool exists = ui_flow_user_has(s_flow.kind, user);
        if ((UI_FLOW_STEP_ADD_ID == s_flow.step) && exists)
        {
            (void)snprintf(detail, sizeof(detail), "User %u already has one", user);
            ui_flow_show(UI_SERVICE_SCREEN_INPUT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "ID in use", detail);
        }
        else if (UI_FLOW_STEP_ADD_ID == s_flow.step)
        {
            s_flow.pending_user = user;
            ui_flow_go_admin_input(UI_FLOW_STEP_ADD_PIN);
        }
        else if (!exists)
        {
            (void)snprintf(detail, sizeof(detail), "User %u", user);
            ui_flow_show(UI_SERVICE_SCREEN_INPUT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Not found", detail);
        }
        else
        {
            if (UI_FLOW_KIND_PIN == s_flow.kind)
            {
                memset(s_pins[user - 1U], 0, sizeof(s_pins[0]));
            }
            else
            {
                s_enrolled[s_flow.kind][user - 1U] = false; /* TODO: also delete it in the reader. */
            }
            ESP_LOGI(UI_FLOW_TAG, "%s: user %u deleted", s_kind_menu_titles[s_flow.kind], user);
            (void)snprintf(detail, sizeof(detail), "User %u", user);
            ui_flow_go_cred_menu();
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Deleted", detail);
        }
    }
    else if (UI_FLOW_STEP_ADD_PIN == s_flow.step)
    {
        if (strlen(p_text) < UI_FLOW_PIN_MIN_LEN)
        {
            (void)snprintf(detail, sizeof(detail), "%u - 8 digits", UI_FLOW_PIN_MIN_LEN);
            ui_flow_show(UI_SERVICE_SCREEN_INPUT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PIN too short", detail);
        }
        else if (0U != ui_flow_find_pin(p_text))
        {
            /* Two users with one PIN could not be told apart in the log. */
            ui_flow_show(UI_SERVICE_SCREEN_INPUT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PIN already used", "Choose another PIN");
        }
        else
        {
            (void)snprintf(s_flow.pending_pin, sizeof(s_flow.pending_pin), "%s", p_text);
            ui_flow_go_admin_input(UI_FLOW_STEP_ADD_CONFIRM);
        }
    }
    else /* UI_FLOW_STEP_ADD_CONFIRM */
    {
        const bool same = ui_flow_text_equal(p_text, s_flow.pending_pin);
        if (same)
        {
            (void)snprintf(s_pins[s_flow.pending_user - 1U], sizeof(s_pins[0]), "%s", s_flow.pending_pin);
        }
        memset(s_flow.pending_pin, 0, sizeof(s_flow.pending_pin));

        if (same)
        {
            ESP_LOGI(UI_FLOW_TAG, "PIN codes: user %u added", s_flow.pending_user);
            (void)snprintf(detail, sizeof(detail), "User %u added", s_flow.pending_user);
            ui_flow_go_cred_menu();
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Saved", detail);
        }
        else
        {
            ui_flow_go_admin_input(UI_FLOW_STEP_ADD_PIN);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PINs do not match", "Enter the new PIN again");
        }
    }
}

/***********************************************************************************************************************
 * Screen changes. Texts are set first, under the lock, so the screen never shows the previous state's text.
 **********************************************************************************************************************/

static void ui_flow_go_home(void)
{
    ui_flow_enter(UI_FLOW_STATE_HOME);
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_TopBarText, UI_FLOW_TOPBAR_USER);
        lv_label_set_text(ui_HomeState, s_flow.unlocked ? "UNLOCKED" : "LOCKED");
        ui_flow_unlock();
    }
    ui_flow_show(UI_SERVICE_SCREEN_HOME);
}

static void ui_flow_go_pin_entry(void)
{
    ui_flow_enter(UI_FLOW_STATE_PIN_ENTRY);
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_InputTitle, "ENTER PIN");
        lv_label_set_text(ui_InputPrompt, "Enter PIN or scan card / fingerprint");
        lv_textarea_set_placeholder_text(ui_InputField, "PIN");
        lv_textarea_set_password_mode(ui_InputField, true);
        lv_label_set_text_fmt(ui_InputHintText, "Returns to home after %us", (unsigned)(UI_FLOW_PIN_IDLE_MS / 1000U));
        ui_flow_unlock();
    }
    ui_flow_show(UI_SERVICE_SCREEN_INPUT); /* Coming from Home: keeps the digit that was pressed. */
}

static void ui_flow_go_lockout(bool admin)
{
    ui_flow_enter(admin ? UI_FLOW_STATE_ADMIN_LOCKOUT : UI_FLOW_STATE_LOCKOUT);
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_StatusIcon, LV_SYMBOL_WARNING);
        lv_label_set_text(ui_StatusTitle, "SECURITY ALERT");
        lv_label_set_text(ui_StatusDetail, admin ? "Too many wrong admin logins" : "Too many failed attempts");
        lv_label_set_text(ui_StatusHintText, "Keypad is disabled until the timer ends");
        ui_flow_unlock();
    }
    ui_flow_update_timed(xTaskGetTickCount());
    ui_flow_show(UI_SERVICE_SCREEN_STATUS);
}

static void ui_flow_go_voice(void)
{
    ESP_LOGI(UI_FLOW_TAG, "Voice recording started");
    /* TODO(audio_service): start recording from the INMP441. */
    ui_flow_enter(UI_FLOW_STATE_VOICE);
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_VoiceTitle, "RECORDING");
        lv_label_set_text(ui_VoiceHintText, "#: Stop        Hold *: Cancel");
        ui_flow_unlock();
    }
    ui_flow_update_timed(xTaskGetTickCount());
    ui_flow_show(UI_SERVICE_SCREEN_VOICE);
}

static void ui_flow_voice_end(bool cancelled)
{
    ESP_LOGI(UI_FLOW_TAG, "Voice recording %s", cancelled ? "cancelled" : "stopped");
    /* TODO(audio_service): stop recording; on success say SUCCESS "Message saved", on failure ERROR "Record failed". */
    ui_flow_go_home();
    if (cancelled)
    {
        ui_flow_popup(UI_SERVICE_POPUP_INFO, "Recording cancelled", NULL);
    }
    else
    {
        ui_flow_popup(UI_SERVICE_POPUP_INFO, "Recording stopped", "Audio not connected yet");
    }
}

static void ui_flow_go_admin_login(void)
{
    ui_flow_enter(UI_FLOW_STATE_ADMIN_LOGIN);
    ui_flow_show(UI_SERVICE_SCREEN_ADMIN_LOGIN);
}

static void ui_flow_go_admin_menu(void)
{
    ui_flow_enter(UI_FLOW_STATE_ADMIN_MENU);
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_TopBarText, UI_FLOW_TOPBAR_ADMIN);
        ui_flow_menu_set("ADMIN MENU", "1    PIN codes", "2    Fingerprints", "3    RFID cards", "0    Exit admin");
        lv_label_set_text(ui_MenuCount, "Admin mode");
        ui_flow_unlock();
    }
    ui_flow_show(UI_SERVICE_SCREEN_MENU);
}

static void ui_flow_go_cred_menu(void)
{
    ui_flow_enter(UI_FLOW_STATE_CRED_MENU);
    if (ui_flow_lock())
    {
        ui_flow_menu_set(s_kind_menu_titles[s_flow.kind], "1    Add", "2    Delete", "0    Back", "");
        lv_label_set_text_fmt(
            ui_MenuCount, "Stored: %u / %u", (unsigned)ui_flow_user_count(s_flow.kind), (unsigned)UI_FLOW_USER_COUNT);
        ui_flow_unlock();
    }
    ui_flow_show(UI_SERVICE_SCREEN_MENU);
}

static void ui_flow_go_admin_input(ui_flow_step_t step)
{
    const bool id_step = (UI_FLOW_STEP_ADD_ID == step) || (UI_FLOW_STEP_DELETE_ID == step);

    ui_flow_enter(UI_FLOW_STATE_ADMIN_INPUT);
    s_flow.step = step;
    if (ui_flow_lock())
    {
        lv_label_set_text(ui_InputTitle,
                          (UI_FLOW_STEP_DELETE_ID == step) ? s_kind_delete_titles[s_flow.kind] : "ADD PIN");
        if (id_step)
        {
            lv_label_set_text_fmt(ui_InputPrompt, "Enter user ID (1-%u)", (unsigned)UI_FLOW_USER_COUNT);
        }
        else
        {
            lv_label_set_text(ui_InputPrompt,
                              (UI_FLOW_STEP_ADD_PIN == step) ? "Enter new PIN (4-8 digits)" : "Enter PIN again");
        }
        lv_textarea_set_placeholder_text(ui_InputField, id_step ? "User ID" : "PIN");
        lv_textarea_set_password_mode(ui_InputField, !id_step);
        lv_label_set_text(ui_InputHintText, "Hold * to go back");
        ui_flow_unlock();
    }
    ui_flow_show(UI_SERVICE_SCREEN_INPUT); /* Also empties the field when the step repeats. */
}

static void ui_flow_go_scan(void)
{
    ui_flow_enter(UI_FLOW_STATE_ADMIN_SCAN);
    if (ui_flow_lock())
    {
        const bool finger = (UI_FLOW_KIND_FINGER == s_flow.kind);
        lv_label_set_text(ui_StatusIcon, LV_SYMBOL_EYE_OPEN);
        lv_label_set_text(ui_StatusTitle, finger ? "ADD FINGERPRINT" : "ADD CARD");
        lv_label_set_text(ui_StatusDetail, finger ? "Place finger on the sensor" : "Tap card on the reader");
        lv_label_set_text(ui_StatusHintText, "Hold * to cancel");
        ui_flow_unlock();
    }
    ui_flow_update_timed(xTaskGetTickCount());
    ui_flow_show(UI_SERVICE_SCREEN_STATUS);
}

static void ui_flow_exit_admin(const char *p_title, const char *p_detail)
{
    memset(s_flow.pending_pin, 0, sizeof(s_flow.pending_pin));
    ui_flow_go_home(); /* Also puts SMART LOCK back in the top bar. */
    if (NULL != p_title)
    {
        ui_flow_popup(UI_SERVICE_POPUP_INFO, p_title, p_detail);
    }
}

/***********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

static void ui_flow_enter(ui_flow_state_t state)
{
    s_flow.state         = state;
    s_flow.state_tick    = xTaskGetTickCount();
    s_flow.activity_tick = s_flow.state_tick;
    s_flow.shown_seconds = UINT32_MAX; /* Countdowns draw at once. */
}

static bool ui_flow_lock(void)
{
    if (APP_SUCCESS == ui_service_lock(s_flow.p_ui, UI_FLOW_LOCK_TIMEOUT_MS))
    {
        return true;
    }
    ESP_LOGW(UI_FLOW_TAG, "LVGL lock timeout, screen text not updated");
    return false;
}

static void ui_flow_unlock(void)
{
    ui_service_unlock(s_flow.p_ui);
}

static void ui_flow_show(ui_service_screen_t screen)
{
    const app_err_t ret = ui_service_show(s_flow.p_ui, screen);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(UI_FLOW_TAG, "ui_service_show(%d) failed with application error: %d", screen, ret);
    }
}

static void ui_flow_popup(ui_service_popup_level_t level, const char *p_title, const char *p_detail)
{
    const app_err_t ret = ui_service_popup(s_flow.p_ui, level, p_title, p_detail, UI_FLOW_POPUP_MS);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(UI_FLOW_TAG, "ui_service_popup failed with application error: %d", ret);
    }
}

static void ui_flow_popup_tries(ui_service_popup_level_t level, const char *p_title, uint8_t tries_left)
{
    char detail[UI_FLOW_DETAIL_SIZE];

    (void)snprintf(detail, sizeof(detail), "%u %s left", tries_left, (1U == tries_left) ? "try" : "tries");
    ui_flow_popup(level, p_title, detail);
}

/* Called under the lock. */
static void ui_flow_menu_set(
    const char *p_title, const char *p_r1, const char *p_r2, const char *p_r3, const char *p_r4)
{
    lv_label_set_text(ui_MenuTitle, p_title);
    lv_label_set_text(ui_MenuRow1, p_r1);
    lv_label_set_text(ui_MenuRow2, p_r2);
    lv_label_set_text(ui_MenuRow3, p_r3);
    lv_label_set_text(ui_MenuRow4, p_r4);
    lv_label_set_text(ui_MenuHint, "Press a number");
}

/* Called under the lock. */
static void ui_flow_bar_set(lv_obj_t *p_fill, uint32_t value, uint32_t max)
{
    lv_obj_set_width(p_fill, (lv_coord_t)(((uint64_t)UI_FLOW_BAR_WIDTH * value) / max));
}

/* Wraps correctly with the tick counter. */
static uint32_t ui_flow_elapsed_ms(TickType_t since, TickType_t now)
{
    return (uint32_t)(now - since) * portTICK_PERIOD_MS;
}

/* Compares every character whatever the first difference, so timing does not leak how much of a PIN was right. */
static bool ui_flow_text_equal(const char *p_a, const char *p_b)
{
    const size_t len_a = strlen(p_a);
    const size_t len_b = strlen(p_b);
    uint8_t      diff  = (len_a != len_b) ? 1U : 0U;

    for (size_t i = 0U; i < (UI_FLOW_TEXT_SIZE - 1U); i++)
    {
        const char char_a = (i < len_a) ? p_a[i] : '\0';
        const char char_b = (i < len_b) ? p_b[i] : '\0';
        diff |= (uint8_t)(char_a ^ char_b);
    }
    return 0U == diff;
}

/* User ID owning this PIN, or 0. */
static uint8_t ui_flow_find_pin(const char *p_pin)
{
    uint8_t found = 0U;

    for (uint8_t i = 0U; i < UI_FLOW_USER_COUNT; i++)
    {
        if (('\0' != s_pins[i][0]) && ui_flow_text_equal(p_pin, s_pins[i]) && (0U == found))
        {
            found = (uint8_t)(i + 1U);
        }
    }
    return found;
}

/* 1..UI_FLOW_USER_COUNT, or 0 when the text is not a valid user ID. */
static uint8_t ui_flow_parse_user(const char *p_text)
{
    char         *p_end = NULL;
    unsigned long value = strtoul(p_text, &p_end, 10);

    if ((p_end == p_text) || ('\0' != *p_end) || (value < 1UL) || (value > UI_FLOW_USER_COUNT))
    {
        return 0U;
    }
    return (uint8_t)value;
}

static bool ui_flow_user_has(ui_flow_kind_t kind, uint8_t user)
{
    return (UI_FLOW_KIND_PIN == kind) ? ('\0' != s_pins[user - 1U][0]) : s_enrolled[kind][user - 1U];
}

static uint8_t ui_flow_user_count(ui_flow_kind_t kind)
{
    uint8_t count = 0U;

    for (uint8_t user = 1U; user <= UI_FLOW_USER_COUNT; user++)
    {
        count += ui_flow_user_has(kind, user) ? 1U : 0U;
    }
    return count;
}

/* Access attempt for the history on the web page. The user is known only when granted. */
static void ui_flow_log_event(event_log_result_t result, uint8_t user)
{
    event_log_instance_ctrl_t *const p_log                     = network_task_get_event_log();
    char                             name[EVENT_LOG_USER_SIZE] = "";

    if (NULL == p_log)
    {
        return;
    }
    if (0U != user)
    {
        (void)snprintf(name, sizeof(name), "User %u", user);
    }

    const app_err_t ret = event_log_add_event(p_log, EVENT_LOG_METHOD_PIN, result, name, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(UI_FLOW_TAG, "event_log_add_event failed with application error: %d", ret);
    }
}

static void ui_flow_log_alert(const char *p_msg)
{
    event_log_instance_ctrl_t *const p_log = network_task_get_event_log();

    if (NULL == p_log)
    {
        return;
    }

    const app_err_t ret
        = event_log_add_alert(p_log, EVENT_LOG_LEVEL_WARNING, EVENT_LOG_ALERT_PIN_BRUTEFORCE, p_msg, NULL);
    if (APP_SUCCESS != ret)
    {
        ESP_LOGW(UI_FLOW_TAG, "event_log_add_alert failed with application error: %d", ret);
    }
}

#else /* UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE */

void boot_ui_flow(void)
{
}

void ui_flow_post_keys(uint16_t pressed_keys, uint16_t previous_keys)
{
    (void)pressed_keys;
    (void)previous_keys;
}

#endif /* UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE */
