/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "ui_flow_task.h"

#if UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "network_task.h"
#include "system_control.h"
#include "ui.h" /* SquareLine objects (ui_*), exported by app/ui_service. */
#include "ui_service.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_FLOW_TAG          "UI_FLOW"
#define UI_FLOW_BAR_WIDTH    240U /* Width of ui_VoiceTrack and ui_StatusTrack. */
#define UI_FLOW_WIFI_POLL_MS 1000U
#define UI_FLOW_DETAIL_SIZE  32U
#define UI_FLOW_TOPBAR_USER  "SMART LOCK"
#define UI_FLOW_TOPBAR_ADMIN "ADMIN MODE"

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Used only by the flow task. The shown_* fields are what was last drawn, so only changes are redrawn. */
typedef struct st_ui_flow_ctx
{
    ui_service_instance_ctrl_t *p_ui;
    bool                        drawn; /* A state has been shown at least once. */
    system_state_t              state;
    system_cred_kind_t          kind;
    system_admin_step_t         step;
    bool                        login_on_password;
    uint32_t                    notice_seq;
    TickType_t                  wifi_tick;
    int32_t                     shown_seconds;
    int16_t                     shown_scans; /* ADMIN_SCAN: scan count last drawn. */
    int32_t                     shown_entry; /* -1: redraw. Length, plus the text for visible fields. */
    char                        shown_text[SYSTEM_ENTRY_MAX + 1U];
    int8_t                      shown_unlocked;
    int16_t                     shown_messages;
    int8_t                      wifi_shown; /* -1 not drawn yet, 0 off, 1 on. */
} ui_flow_ctx_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void ui_flow_task(void *pvParameter);
static void ui_flow_enter(const system_view_t *const p_view);
static void ui_flow_refresh(const system_view_t *const p_view);
static void ui_flow_refresh_field(lv_obj_t *p_field, const system_view_t *const p_view, bool secret);
static void ui_flow_refresh_countdown(const system_view_t *const p_view);
static void ui_flow_notice(const system_view_t *const p_view);
static void ui_flow_set_input(const system_view_t *const p_view);
static void ui_flow_set_status(const char *p_icon, const char *p_title, const char *p_detail, const char *p_hint);
static void ui_flow_menu_set(
    const char *p_title, const char *p_r1, const char *p_r2, const char *p_r3, const char *p_r4);
static void     ui_flow_wifi(TickType_t now);
static bool     ui_flow_lock(void);
static void     ui_flow_unlock(void);
static void     ui_flow_show(ui_service_screen_t screen);
static void     ui_flow_popup(ui_service_popup_level_t level, const char *p_title, const char *p_detail);
static uint32_t ui_flow_elapsed_ms(TickType_t since, TickType_t now);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static ui_flow_ctx_t s_flow = {0};

static const char *const s_kind_menu_titles[SYSTEM_CRED_COUNT]   = {"PIN CODES", "FINGERPRINTS", "RFID CARDS"};
static const char *const s_kind_delete_titles[SYSTEM_CRED_COUNT] = {"DELETE PIN", "DELETE FINGERPRINT", "DELETE CARD"};
static const char *const s_kind_names[SYSTEM_CRED_COUNT]         = {"PIN", "fingerprint", "card"};

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
    s_flow.wifi_shown = -1;
    if (pdPASS != xTaskCreate(ui_flow_task, "ui_flow", UI_FLOW_TASK_STACK, NULL, UI_FLOW_TASK_PRIORITY, NULL))
    {
        ESP_LOGE(UI_FLOW_TAG, "Failed to create the screen flow task");
    }
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Every UI_FLOW_TICK_MS: reads system_control, changes screen when its state changed, redraws what moved, shows a
 * popup for a new notice.
 **********************************************************************************************************************/
static void ui_flow_task(void *pvParameter)
{
    (void)pvParameter;
    system_view_t view;

    if (ui_flow_lock())
    {
        /* The fields show placeholders as long as the code, never the code: no last-key preview. */
        lv_textarea_set_password_show_time(ui_InputField, 0);
        lv_textarea_set_password_show_time(ui_AdminLoginPass, 0);
        ui_flow_unlock();
    }

    while (1)
    {
        if (system_control_get_view(&view))
        {
            const bool changed = !s_flow.drawn || (view.state != s_flow.state) || (view.kind != s_flow.kind)
                                 || (view.step != s_flow.step);
            if (changed)
            {
                ui_flow_enter(&view);
            }
            ui_flow_refresh(&view);
            if (view.notice_seq != s_flow.notice_seq)
            {
                s_flow.notice_seq = view.notice_seq;
                ui_flow_notice(&view); /* After the screen change: the popup stays above the new screen. */
            }
        }
        ui_flow_wifi(xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(UI_FLOW_TICK_MS));
    }
}

/***********************************************************************************************************************
 * A new state: set the fixed texts first, under the lock, then show the screen, so it never shows the previous
 * state's text. Shown again for the same screen it empties its fields.
 **********************************************************************************************************************/
static void ui_flow_enter(const system_view_t *const p_view)
{
    ui_service_screen_t screen = UI_SERVICE_SCREEN_HOME;
    const bool          admin  = (SYSTEM_STATE_ADMIN_MENU == p_view->state) || (SYSTEM_STATE_CRED_MENU == p_view->state)
                       || (SYSTEM_STATE_ADMIN_INPUT == p_view->state) || (SYSTEM_STATE_ADMIN_SCAN == p_view->state);

    if (!ui_flow_lock())
    {
        return; /* Not drawn: the next tick tries again. */
    }
    lv_label_set_text(ui_TopBarText, admin ? UI_FLOW_TOPBAR_ADMIN : UI_FLOW_TOPBAR_USER);

    switch (p_view->state)
    {
        case SYSTEM_STATE_PIN_ENTRY:
        case SYSTEM_STATE_ADMIN_INPUT:
            ui_flow_set_input(p_view);
            screen = UI_SERVICE_SCREEN_INPUT;
            break;

        case SYSTEM_STATE_VOICE:
            lv_label_set_text(ui_VoiceTitle, "RECORDING");
            lv_label_set_text(ui_VoiceHintText, "#: Stop        Hold *: Cancel");
            screen = UI_SERVICE_SCREEN_VOICE;
            break;

        case SYSTEM_STATE_ADMIN_LOGIN:
            screen = UI_SERVICE_SCREEN_ADMIN_LOGIN;
            break;

        case SYSTEM_STATE_ADMIN_MENU:
            ui_flow_menu_set("ADMIN MENU", "1    PIN codes", "2    Fingerprints", "3    RFID cards", "0    Exit admin");
            lv_label_set_text(ui_MenuCount, "Admin mode");
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_CRED_MENU:
            ui_flow_menu_set(s_kind_menu_titles[p_view->kind], "1    Add", "2    Delete", "0    Back", "");
            lv_label_set_text_fmt(
                ui_MenuCount, "Stored: %u / %u", (unsigned)p_view->stored_count, (unsigned)SYSTEM_USER_COUNT);
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_ADMIN_SCAN:
        {
            const bool finger = (SYSTEM_CRED_FINGER == p_view->kind);
            ui_flow_set_status(LV_SYMBOL_EYE_OPEN,
                               finger ? "ADD FINGERPRINT" : "ADD CARD",
                               finger ? "Place finger on the sensor" : "Tap card on the reader",
                               "Hold * to cancel");
            screen = UI_SERVICE_SCREEN_STATUS;
            break;
        }

        case SYSTEM_STATE_LOCKOUT:
        case SYSTEM_STATE_ADMIN_LOCKOUT:
            ui_flow_set_status(LV_SYMBOL_WARNING,
                               "SECURITY ALERT",
                               (SYSTEM_STATE_ADMIN_LOCKOUT == p_view->state) ? "Too many wrong admin logins"
                                                                             : "Too many failed attempts",
                               p_view->lock_forever ? "Locked until restart"
                                                    : "Keypad is disabled until the timer ends");
            if (p_view->lock_forever)
            {
                lv_label_set_text(ui_StatusValue, "Contact the administrator");
                lv_obj_set_width(ui_StatusFill, UI_FLOW_BAR_WIDTH);
            }
            screen = UI_SERVICE_SCREEN_STATUS;
            break;

        default: /* SYSTEM_STATE_HOME */
            break;
    }
    ui_flow_unlock();
    ui_flow_show(screen);

    s_flow.drawn             = true;
    s_flow.state             = p_view->state;
    s_flow.kind              = p_view->kind;
    s_flow.step              = p_view->step;
    s_flow.login_on_password = false; /* ui_service_show() gave the keys to the ID. */
    s_flow.shown_seconds     = -1;
    s_flow.shown_scans       = -1;
    s_flow.shown_entry       = -1;
    s_flow.shown_unlocked    = -1;
    s_flow.shown_messages    = -1;
}

/* What changes inside a state: typed characters, the login field with the keys, UNLOCKED, messages, countdowns. */
static void ui_flow_refresh(const system_view_t *const p_view)
{
    switch (p_view->state)
    {
        case SYSTEM_STATE_HOME:
        {
            const int8_t  unlocked = p_view->door_unlocked ? 1 : 0;
            const int16_t messages = p_view->message_count;
            if (((unlocked != s_flow.shown_unlocked) || (messages != s_flow.shown_messages)) && ui_flow_lock())
            {
                lv_label_set_text(ui_HomeState, (0 != unlocked) ? "UNLOCKED" : "LOCKED");
                if (0 == messages)
                {
                    lv_label_set_text(ui_HomeMessages, "No new messages");
                }
                else
                {
                    lv_label_set_text_fmt(
                        ui_HomeMessages, "%u new message%s", (unsigned)messages, (1 == messages) ? "" : "s");
                }
                ui_flow_unlock();
                s_flow.shown_unlocked = unlocked;
                s_flow.shown_messages = messages;
            }
            break;
        }

        case SYSTEM_STATE_PIN_ENTRY:
            ui_flow_refresh_field(ui_InputField, p_view, true);
            break;

        case SYSTEM_STATE_ADMIN_INPUT:
            ui_flow_refresh_field(ui_InputField,
                                  p_view,
                                  (SYSTEM_STEP_ADD_PIN == p_view->step) || (SYSTEM_STEP_ADD_CONFIRM == p_view->step));
            break;

        case SYSTEM_STATE_ADMIN_LOGIN:
            if ((p_view->login_on_password != s_flow.login_on_password) && ui_flow_lock())
            {
                /* The ID field keeps the ID while the password is typed. */
                lv_textarea_set_text(ui_AdminLoginId, p_view->login_id);
                if (!p_view->login_on_password)
                {
                    lv_textarea_set_text(ui_AdminLoginPass, "");
                }
                lv_group_focus_obj(p_view->login_on_password ? ui_AdminLoginPass : ui_AdminLoginId);
                ui_flow_unlock();
                s_flow.login_on_password = p_view->login_on_password;
                s_flow.shown_entry       = -1;
            }
            ui_flow_refresh_field(
                p_view->login_on_password ? ui_AdminLoginPass : ui_AdminLoginId, p_view, p_view->login_on_password);
            break;

        default:
            ui_flow_refresh_countdown(p_view);
            break;
    }
}

/* Visible fields show the text; secret ones as many placeholder characters as typed, never the code. */
static void ui_flow_refresh_field(lv_obj_t *p_field, const system_view_t *const p_view, bool secret)
{
    static const char s_fill[SYSTEM_ENTRY_MAX + 1U] = "00000000";

    if (((int32_t)p_view->entry_len == s_flow.shown_entry)
        && (secret || (0 == strcmp(p_view->entry_text, s_flow.shown_text))))
    {
        return;
    }
    if (!ui_flow_lock())
    {
        return;
    }
    lv_textarea_set_text(p_field, secret ? &s_fill[SYSTEM_ENTRY_MAX - p_view->entry_len] : p_view->entry_text);
    ui_flow_unlock();
    s_flow.shown_entry = (int32_t)p_view->entry_len;
    memcpy(s_flow.shown_text, p_view->entry_text, sizeof(s_flow.shown_text));
}

/* Voice time, scan and lockout countdowns: redrawn once per second. */
static void ui_flow_refresh_countdown(const system_view_t *const p_view)
{
    uint32_t total_ms;

    switch (p_view->state)
    {
        case SYSTEM_STATE_VOICE:
            total_ms = SYSTEM_VOICE_MAX_MS;
            break;
        case SYSTEM_STATE_ADMIN_SCAN:
            total_ms = SYSTEM_SCAN_TIMEOUT_MS;
            break;
        case SYSTEM_STATE_LOCKOUT:
        case SYSTEM_STATE_ADMIN_LOCKOUT:
            if (p_view->lock_forever)
            {
                return;
            }
            total_ms = SYSTEM_LOCKOUT_MS;
            break;
        default:
            return;
    }

    const uint32_t elapsed_ms = (p_view->state_ms < total_ms) ? p_view->state_ms : total_ms;
    const uint32_t remain_ms  = total_ms - elapsed_ms;
    const bool     voice      = (SYSTEM_STATE_VOICE == p_view->state);
    const int32_t  seconds    = (int32_t)(voice ? (elapsed_ms / 1000U) : ((remain_ms + 999U) / 1000U));

    const bool scans_changed = (SYSTEM_STATE_ADMIN_SCAN == p_view->state) && (p_view->scan_count != s_flow.shown_scans);
    if (((seconds == s_flow.shown_seconds) && !scans_changed) || !ui_flow_lock())
    {
        return;
    }
    switch (p_view->state)
    {
        case SYSTEM_STATE_VOICE:
            lv_label_set_text_fmt(
                ui_VoiceTime, "00:%02u / 00:%02u", (unsigned)seconds, (unsigned)(SYSTEM_VOICE_MAX_MS / 1000U));
            lv_obj_set_width(ui_VoiceFill, (lv_coord_t)(((uint64_t)UI_FLOW_BAR_WIDTH * elapsed_ms) / total_ms));
            break;
        case SYSTEM_STATE_ADMIN_SCAN:
        {
            /* The bar shows the scans done; the hint the time left for this scan. */
            const bool finger = (SYSTEM_CRED_FINGER == p_view->kind);
            if (0U == p_view->scan_count)
            {
                lv_label_set_text(ui_StatusDetail, finger ? "Place finger on the sensor" : "Tap card on the reader");
            }
            else
            {
                lv_label_set_text(ui_StatusDetail,
                                  finger ? "Lift, then place the same finger" : "Lift, then tap the same card");
            }
            lv_label_set_text_fmt(ui_StatusValue,
                                  "User %u    Scan %u / %u",
                                  (unsigned)p_view->scan_user,
                                  (unsigned)(p_view->scan_count + 1U),
                                  (unsigned)SYSTEM_SCAN_COUNT);
            lv_label_set_text_fmt(ui_StatusHintText, "Hold * to cancel    %u s", (unsigned)seconds);
            lv_obj_set_width(ui_StatusFill,
                             (lv_coord_t)((UI_FLOW_BAR_WIDTH * (uint32_t)p_view->scan_count) / SYSTEM_SCAN_COUNT));
            s_flow.shown_scans = p_view->scan_count;
            break;
        }
        break;
        default:
            lv_label_set_text_fmt(ui_StatusValue,
                                  "%s locked for %u s",
                                  (SYSTEM_STATE_LOCKOUT == p_view->state) ? "Door" : "Admin",
                                  (unsigned)seconds);
            lv_obj_set_width(ui_StatusFill, (lv_coord_t)(((uint64_t)UI_FLOW_BAR_WIDTH * remain_ms) / total_ms));
            break;
    }
    ui_flow_unlock();
    s_flow.shown_seconds = seconds;
}

/* Popup texts of docs/ui_data.md for each system_control notice. */
static void ui_flow_notice(const system_view_t *const p_view)
{
    char           detail[UI_FLOW_DETAIL_SIZE];
    const unsigned value = p_view->notice_value;

    switch (p_view->notice)
    {
        case SYSTEM_NOTICE_UNLOCKED:
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Welcome", "Door unlocked");
            break;
        case SYSTEM_NOTICE_PIN_WRONG:
        case SYSTEM_NOTICE_ADMIN_WRONG:
            (void)snprintf(detail, sizeof(detail), "%u %s left", value, (1U == value) ? "try" : "tries");
            ui_flow_popup(UI_SERVICE_POPUP_ERROR,
                          (SYSTEM_NOTICE_PIN_WRONG == p_view->notice) ? "Authentication failed"
                                                                      : "Wrong ID or password",
                          detail);
            break;
        case SYSTEM_NOTICE_CARD_UNKNOWN:
        case SYSTEM_NOTICE_FINGER_UNKNOWN:
            (void)snprintf(detail,
                           sizeof(detail),
                           "Unknown %s, %u %s left",
                           (SYSTEM_NOTICE_CARD_UNKNOWN == p_view->notice) ? "card" : "finger",
                           value,
                           (1U == value) ? "try" : "tries");
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Authentication failed", detail);
            break;
        case SYSTEM_NOTICE_ADMIN_TIMEOUT:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Admin mode ended", "No key pressed");
            break;
        case SYSTEM_NOTICE_VOICE_STOPPED:
            /* TODO(audio_service): SUCCESS "Message saved" / ERROR "Record failed" once recording is real. */
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Recording stopped", NULL);
            break;
        case SYSTEM_NOTICE_VOICE_CANCELLED:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Recording cancelled", NULL);
            break;
        case SYSTEM_NOTICE_NO_MESSAGES:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "No new messages", NULL);
            break;
        case SYSTEM_NOTICE_PLAYING:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Playing message", NULL);
            break;
        case SYSTEM_NOTICE_INVALID_ID:
            (void)snprintf(detail, sizeof(detail), "Use 1 - %u", (unsigned)SYSTEM_USER_COUNT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Invalid user ID", detail);
            break;
        case SYSTEM_NOTICE_ID_IN_USE:
            (void)snprintf(detail, sizeof(detail), "User %u already has one", value);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "ID in use", detail);
            break;
        case SYSTEM_NOTICE_NOT_FOUND:
            (void)snprintf(detail, sizeof(detail), "User %u", value);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Not found", detail);
            break;
        case SYSTEM_NOTICE_DELETED:
            (void)snprintf(detail, sizeof(detail), "User %u", value);
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Deleted", detail);
            break;
        case SYSTEM_NOTICE_PIN_TOO_SHORT:
            (void)snprintf(detail, sizeof(detail), "%u - %u digits", SYSTEM_PIN_MIN_LEN, SYSTEM_ENTRY_MAX);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PIN too short", detail);
            break;
        case SYSTEM_NOTICE_PIN_USED:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PIN already used", "Choose another PIN");
            break;
        case SYSTEM_NOTICE_PIN_MISMATCH:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "PINs do not match", "Enter the new PIN again");
            break;
        case SYSTEM_NOTICE_SAVED:
            (void)snprintf(detail, sizeof(detail), "User %u added", value);
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Saved", detail);
            break;
        case SYSTEM_NOTICE_FULL:
            (void)snprintf(detail, sizeof(detail), "All %u user IDs used", (unsigned)SYSTEM_USER_COUNT);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Failed", detail);
            break;
        case SYSTEM_NOTICE_CARD_IN_USE:
            (void)snprintf(detail, sizeof(detail), "Card of user %u", value);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Already added", detail);
            break;
        case SYSTEM_NOTICE_SCAN_MISMATCH:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Cards do not match", "Counting again with this card");
            break;
        case SYSTEM_NOTICE_SCAN_BAD:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Finger not read", "Place it again");
            break;
        case SYSTEM_NOTICE_SCAN_TIMEOUT:
            (void)snprintf(detail, sizeof(detail), "No %s detected", s_kind_names[p_view->kind]);
            /* Same text for both: once the fingerprint driver calls system_control_finger_scanned(), a timeout really
             * means no finger was placed. */
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Failed", detail);
            break;
        default:
            break;
    }
}

/* Called under the lock. Input serves ENTER PIN and the admin steps. */
static void ui_flow_set_input(const system_view_t *const p_view)
{
    const bool pin_entry = (SYSTEM_STATE_PIN_ENTRY == p_view->state);
    const bool id_step
        = !pin_entry && ((SYSTEM_STEP_ADD_ID == p_view->step) || (SYSTEM_STEP_DELETE_ID == p_view->step));

    lv_label_set_text(ui_InputKeys, "#  OK          *  Delete");
    if (pin_entry)
    {
        lv_label_set_text(ui_InputTitle, "ENTER PIN");
        lv_label_set_text(ui_InputPrompt, "Enter PIN or scan card / fingerprint");
        lv_label_set_text_fmt(ui_InputHintText, "Returns to home after %us", (unsigned)(SYSTEM_PIN_IDLE_MS / 1000U));
    }
    else
    {
        lv_label_set_text(ui_InputTitle,
                          (SYSTEM_STEP_DELETE_ID == p_view->step) ? s_kind_delete_titles[p_view->kind] : "ADD PIN");
        if (id_step)
        {
            lv_label_set_text_fmt(ui_InputPrompt, "Enter user ID (1-%u)", (unsigned)SYSTEM_USER_COUNT);
        }
        else
        {
            lv_label_set_text(ui_InputPrompt,
                              (SYSTEM_STEP_ADD_PIN == p_view->step) ? "Enter new PIN (4-8 digits)" : "Enter PIN again");
        }
        lv_label_set_text(ui_InputHintText, "Hold * to go back");
    }
    lv_textarea_set_placeholder_text(ui_InputField, id_step ? "User ID" : "PIN");
    lv_textarea_set_password_mode(ui_InputField, !id_step);
    /* Home -> Input keeps the field (ui_service_show()): empty it here, or the last entry would flash until
     * ui_flow_refresh() draws this one. */
    lv_textarea_set_text(ui_InputField, "");
}

/* Called under the lock. */
static void ui_flow_set_status(const char *p_icon, const char *p_title, const char *p_detail, const char *p_hint)
{
    lv_label_set_text(ui_StatusIcon, p_icon);
    lv_label_set_text(ui_StatusTitle, p_title);
    lv_label_set_text(ui_StatusDetail, p_detail);
    lv_label_set_text(ui_StatusHintText, p_hint);
    lv_label_set_text(ui_StatusValue, "");
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

/* The red mark in the top bar while Wi-Fi is down. */
static void ui_flow_wifi(TickType_t now)
{
    if ((s_flow.wifi_shown >= 0) && (ui_flow_elapsed_ms(s_flow.wifi_tick, now) < UI_FLOW_WIFI_POLL_MS))
    {
        return;
    }
    s_flow.wifi_tick       = now;
    const int8_t connected = network_task_is_wifi_connected() ? 1 : 0;
    if ((connected != s_flow.wifi_shown) && ui_flow_lock())
    {
        lv_label_set_text(ui_TopBarWifiOff, (0 != connected) ? "" : LV_SYMBOL_CLOSE);
        ui_flow_unlock();
        s_flow.wifi_shown = connected;
    }
}

/***********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

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

/* Wraps correctly with the tick counter. */
static uint32_t ui_flow_elapsed_ms(TickType_t since, TickType_t now)
{
    return (uint32_t)(now - since) * portTICK_PERIOD_MS;
}

#else /* UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE */

void boot_ui_flow(void)
{
}

#endif /* UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE */
