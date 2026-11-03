/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/
#include "ui_flow_task.h"

#if UI_TASK_ENABLED && UI_SERVICE_HAS_SQUARELINE
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
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
#define UI_FLOW_DETAIL_SIZE  40U
#define UI_FLOW_LIST_ROWS    4U /* ui_MenuRow1..4: the window of a long list. */
#define UI_FLOW_ROW_STEP     26 /* Pixels between two rows of ui_MenuList. */
#define UI_FLOW_ROW_TOP      (-39)
#define UI_FLOW_SCROLL_H     100 /* Height of the scroll track in ui_MenuList. */
#define UI_FLOW_TOPBAR_USER  "SMART LOCK"
#define UI_FLOW_TOPBAR_ADMIN "ADMIN MODE"
#define UI_FLOW_ICON_SIZE    8U
#define UI_FLOW_PCT_SIZE     8U

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
    int16_t                     shown_sel;   /* ADMIN_LIST: highlighted row last drawn. */
    int16_t                     shown_count; /* ADMIN_LIST: rows last drawn. */
    uint8_t                     list_top;    /* ADMIN_LIST: first row in the window. */
    system_list_purpose_t       purpose;
    int32_t                     shown_entry; /* -1: redraw. Length, plus the text for visible fields. */
    char                        shown_text[SYSTEM_ENTRY_MAX + 1U];
    int8_t                      shown_unlocked;
    int16_t                     shown_messages;
    uint32_t                    shown_msg_sig; /* MSG_LIST: the messages last drawn. */
    int8_t                      wifi_shown;    /* -1 not drawn yet, 0 off, 1 on. */
    int16_t                     battery_shown; /* -1 not drawn yet, else the percent (or UI_FLOW_BATTERY_UNKNOWN). */
} ui_flow_ctx_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static void     ui_flow_task(void *pvParameter);
static void     ui_flow_enter(const system_view_t *const p_view);
static void     ui_flow_refresh(const system_view_t *const p_view);
static void     ui_flow_refresh_field(lv_obj_t *p_field, const system_view_t *const p_view, bool secret);
static void     ui_flow_refresh_countdown(const system_view_t *const p_view);
static void     ui_flow_notice(const system_view_t *const p_view);
static void     ui_flow_set_input(const system_view_t *const p_view);
static void     ui_flow_list_create(void);
static void     ui_flow_list_show(bool show);
static void     ui_flow_list_draw(const system_view_t *const p_view);
static void     ui_flow_item_text(const system_view_t *const p_view,
                                  const system_list_item_t  *p_item,
                                  char                      *p_out,
                                  size_t                     size);
static void     ui_flow_owner_text(system_cred_kind_t kind, uint8_t user, uint8_t finger, char *p_out, size_t size);
static void     ui_flow_row_text(const system_view_t *const p_view, uint8_t index, char *p_out, size_t size);
static void     ui_flow_msg_text(const system_message_t *p_msg, uint8_t index, char *p_out, size_t size);
static uint32_t ui_flow_msg_sig(const system_view_t *const p_view);
static void     ui_flow_set_status(const char *p_icon, const char *p_title, const char *p_detail, const char *p_hint);
static void     ui_flow_menu_set(
        const char *p_title, const char *p_r1, const char *p_r2, const char *p_r3, const char *p_r4);
static void     ui_flow_wifi(TickType_t now);
static void     ui_flow_battery(void);
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
static const char *const s_kind_add_titles[SYSTEM_CRED_COUNT]    = {"ADD PIN", "ADD FINGERPRINT", "ADD CARD"};

/* Made here, not in SquareLine: the highlight and the scroll bar of an admin list, inside ui_MenuList. */
static lv_obj_t *s_list_highlight = NULL;
static lv_obj_t *s_list_track     = NULL;
static lv_obj_t *s_list_thumb     = NULL;

/* The microphone icon SquareLine put on the Voice screen; PLAYING shows a speaker instead. */
static char s_voice_icon[UI_FLOW_ICON_SIZE] = "";

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

    s_flow.p_ui          = p_ui;
    s_flow.wifi_shown    = -1;
    s_flow.battery_shown = -1;
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
    static system_view_t view; /* ~400 bytes: off the stack. */

    /* Retried until it works: LVGL is often busy at boot, and the list widgets made here must exist before a list is
     * drawn (a skipped setup left them NULL and the first list crashed the board). */
    while (!ui_flow_lock())
    {
        vTaskDelay(pdMS_TO_TICKS(UI_FLOW_TICK_MS));
    }
    {
        /* The fields show placeholders as long as the code, never the code: no last-key preview. */
        lv_textarea_set_password_show_time(ui_InputField, 0);
        lv_textarea_set_password_show_time(ui_AdminLoginPass, 0);
        ui_flow_list_create();
        (void)snprintf(s_voice_icon, sizeof(s_voice_icon), "%s", lv_label_get_text(ui_VoiceIcon));
        ui_flow_unlock();
    }

    while (1)
    {
        if (system_control_get_view(&view))
        {
            const bool changed = !s_flow.drawn || (view.state != s_flow.state) || (view.kind != s_flow.kind)
                                 || ((SYSTEM_STATE_ADMIN_LIST == view.state) && (view.list_purpose != s_flow.purpose))
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
        ui_flow_battery();
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
    const bool          admin
        = (SYSTEM_STATE_ADMIN_MENU == p_view->state) || (SYSTEM_STATE_CRED_MENU == p_view->state)
          || (SYSTEM_STATE_ADMIN_INPUT == p_view->state) || (SYSTEM_STATE_ADMIN_SCAN == p_view->state)
          || (SYSTEM_STATE_ADMIN_LIST == p_view->state) || (SYSTEM_STATE_ADMIN_CONFIRM == p_view->state)
          || (SYSTEM_STATE_MSG_MENU == p_view->state) || (SYSTEM_STATE_MSG_DELETE == p_view->state)
          || (p_view->msg_admin
              && ((SYSTEM_STATE_VOICE == p_view->state) || (SYSTEM_STATE_MSG_LIST == p_view->state)
                  || (SYSTEM_STATE_PLAYING == p_view->state) || (SYSTEM_STATE_VOICE_REVIEW == p_view->state)));

    if (!ui_flow_lock())
    {
        return; /* Not drawn: the next tick tries again. */
    }
    lv_label_set_text(ui_TopBarText, admin ? UI_FLOW_TOPBAR_ADMIN : UI_FLOW_TOPBAR_USER);
    ui_flow_list_show((SYSTEM_STATE_ADMIN_LIST == p_view->state) || (SYSTEM_STATE_MSG_LIST == p_view->state));
    lv_label_set_text(ui_VoiceIcon, (SYSTEM_STATE_PLAYING == p_view->state) ? LV_SYMBOL_VOLUME_MAX : s_voice_icon);
    lv_obj_clear_flag(ui_StatusTrack, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_StatusFill, LV_OBJ_FLAG_HIDDEN);

    switch (p_view->state)
    {
        case SYSTEM_STATE_PIN_ENTRY:
        case SYSTEM_STATE_ADMIN_INPUT:
            ui_flow_set_input(p_view);
            screen = UI_SERVICE_SCREEN_INPUT;
            break;

        case SYSTEM_STATE_VOICE:
            lv_label_set_text(ui_VoiceTitle, p_view->msg_admin ? "MESSAGE FOR GUESTS" : "RECORDING");
            lv_label_set_text(ui_VoiceHintText, "#: Stop        Hold *: Cancel");
            lv_label_set_text_fmt(ui_VoiceTime, "00:00 / 00:%02u", (unsigned)(SYSTEM_VOICE_MAX_MS / 1000U));
            lv_obj_set_width(ui_VoiceFill, 0);
            screen = UI_SERVICE_SCREEN_VOICE;
            break;

        case SYSTEM_STATE_PLAYING:
            lv_label_set_text(ui_VoiceTitle, "PLAYING");
            lv_label_set_text(ui_VoiceHintText, "# or *: Stop");
            lv_label_set_text_fmt(ui_VoiceTime, "00:00 / 00:%02u", (unsigned)p_view->play_seconds);
            lv_obj_set_width(ui_VoiceFill, 0);
            screen = UI_SERVICE_SCREEN_VOICE;
            break;

        case SYSTEM_STATE_MSG_MENU:
            ui_flow_menu_set("MESSAGES FOR GUESTS", "1    Record new", "2    Listen / delete", "0    Back", "");
            lv_label_set_text_fmt(
                ui_MenuCount, "Stored: %u / %u", (unsigned)p_view->message_count, (unsigned)SYSTEM_MSG_SLOTS);
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_MSG_LIST:
            lv_label_set_text(ui_MenuTitle, p_view->msg_admin ? "MESSAGES FOR GUESTS" : "MESSAGES FROM THE OWNER");
            lv_label_set_text(ui_MenuHint,
                              p_view->msg_admin ? "2/8  #: Play  0: Delete  *: Back" : "2/8: Move   #: Play   *: Back");
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_MSG_DELETE:
        {
            char line[UI_FLOW_DETAIL_SIZE];
            ui_flow_msg_text(&p_view->messages[p_view->list_sel], p_view->list_sel, line, sizeof(line));
            ui_flow_set_status(LV_SYMBOL_TRASH, "DELETE MESSAGE?", line, "#: Delete          *: Cancel");
            lv_obj_add_flag(ui_StatusTrack, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_StatusFill, LV_OBJ_FLAG_HIDDEN);
            screen = UI_SERVICE_SCREEN_STATUS;
            break;
        }

        case SYSTEM_STATE_VOICE_REVIEW:
            /* Detail and length: ui_flow_refresh(), once the voice code has the length. */
            ui_flow_set_status(LV_SYMBOL_AUDIO,
                               p_view->msg_admin ? "MESSAGE FOR GUESTS" : "MESSAGE RECORDED",
                               "Saving...",
                               p_view->msg_admin ? "#: Save          Hold *: Delete"
                                                 : "#: Send          Hold *: Delete");
            lv_obj_add_flag(ui_StatusTrack, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_StatusFill, LV_OBJ_FLAG_HIDDEN);
            screen = UI_SERVICE_SCREEN_STATUS;
            break;

        case SYSTEM_STATE_ADMIN_LOGIN:
            screen = UI_SERVICE_SCREEN_ADMIN_LOGIN;
            break;

        case SYSTEM_STATE_ADMIN_MENU:
            ui_flow_menu_set(
                "ADMIN MENU", "1    PIN codes", "2    Fingerprints", "3    RFID cards", "4    Guest messages");
            lv_label_set_text(ui_MenuCount, "Admin mode");
            lv_label_set_text(ui_MenuHint, "Press a number    0: Exit admin");
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_CRED_MENU:
            ui_flow_menu_set(s_kind_menu_titles[p_view->kind], "1    Add", "2    Delete", "0    Back", "");
            lv_label_set_text_fmt(ui_MenuCount,
                                  "Stored: %u / %u",
                                  (unsigned)p_view->stored_count,
                                  (unsigned)((SYSTEM_CRED_PIN == p_view->kind)    ? SYSTEM_USER_COUNT
                                             : (SYSTEM_CRED_CARD == p_view->kind) ? SYSTEM_CARD_SLOTS
                                                                                  : SYSTEM_FINGER_SLOTS));
            screen = UI_SERVICE_SCREEN_MENU;
            break;

        case SYSTEM_STATE_ADMIN_LIST:
        {
            char line[UI_FLOW_DETAIL_SIZE];
            if (SYSTEM_LIST_DELETE == p_view->list_purpose)
            {
                lv_label_set_text(ui_MenuTitle, s_kind_delete_titles[p_view->kind]);
                (void)snprintf(line, sizeof(line), "Pick what to delete");
            }
            else if (SYSTEM_LIST_PICK_FINGER == p_view->list_purpose)
            {
                lv_label_set_text(ui_MenuTitle, s_kind_add_titles[p_view->kind]);
                (void)snprintf(
                    line, sizeof(line), "Which finger of %s?", system_control_user_name(p_view->pending_user));
            }
            else
            {
                lv_label_set_text(ui_MenuTitle, s_kind_add_titles[p_view->kind]);
                (void)snprintf(line, sizeof(line), "For which user?");
            }
            lv_label_set_text(ui_MenuCount, line);
            lv_label_set_text(ui_MenuHint, "2/8: Move   #: Select   *: Back");
            screen = UI_SERVICE_SCREEN_MENU;
            break;
        }

        case SYSTEM_STATE_ADMIN_CONFIRM:
        {
            char owner[UI_FLOW_DETAIL_SIZE];
            char title[UI_FLOW_DETAIL_SIZE];
            ui_flow_item_text(p_view, &p_view->list[p_view->list_sel], owner, sizeof(owner));
            (void)snprintf(title, sizeof(title), "%s?", s_kind_delete_titles[p_view->kind]);
            ui_flow_set_status(LV_SYMBOL_TRASH, title, owner, "#: Delete          *: Cancel");
            lv_obj_add_flag(ui_StatusTrack, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui_StatusFill, LV_OBJ_FLAG_HIDDEN);
            screen = UI_SERVICE_SCREEN_STATUS;
            break;
        }

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
    s_flow.shown_sel         = -1;
    s_flow.shown_count       = -1;
    s_flow.list_top          = 0U;
    s_flow.purpose           = p_view->list_purpose;
    s_flow.shown_entry       = -1;
    s_flow.shown_unlocked    = -1;
    s_flow.shown_messages    = -1;
    s_flow.shown_msg_sig     = 0U;
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
                    lv_label_set_text(ui_HomeMessages, "No messages");
                }
                else
                {
                    lv_label_set_text_fmt(ui_HomeMessages,
                                          "%u message%s - hold # to listen",
                                          (unsigned)messages,
                                          (1 == messages) ? "" : "s");
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
            ui_flow_refresh_field(ui_InputField, p_view, true); /* A new PIN, typed twice. */
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

        case SYSTEM_STATE_ADMIN_LIST:
            if ((p_view->list_sel != s_flow.shown_sel) || (p_view->list_count != s_flow.shown_count))
            {
                ui_flow_list_draw(p_view);
            }
            break;

        case SYSTEM_STATE_VOICE_REVIEW:
            if (((int32_t)p_view->review_seconds != s_flow.shown_seconds) && (0U != p_view->review_seconds)
                && ui_flow_lock())
            {
                lv_label_set_text(ui_StatusDetail, "1: Listen        2: Record again");
                lv_label_set_text_fmt(ui_StatusValue, "Length: %u s", (unsigned)p_view->review_seconds);
                ui_flow_unlock();
                s_flow.shown_seconds = (int32_t)p_view->review_seconds;
            }
            break;

        case SYSTEM_STATE_MSG_DELETE:
        {
            /* A message from the web moved the rows: show the one that will be deleted. */
            const uint32_t sig = ui_flow_msg_sig(p_view);
            if ((sig != s_flow.shown_msg_sig) && ui_flow_lock())
            {
                char line[UI_FLOW_DETAIL_SIZE];
                ui_flow_msg_text(&p_view->messages[p_view->list_sel], p_view->list_sel, line, sizeof(line));
                lv_label_set_text(ui_StatusDetail, line);
                ui_flow_unlock();
                s_flow.shown_msg_sig = sig;
            }
            break;
        }

        case SYSTEM_STATE_MSG_MENU:
            /* A recording just kept, or a message from the web. */
            if ((p_view->message_count != s_flow.shown_messages) && ui_flow_lock())
            {
                lv_label_set_text_fmt(
                    ui_MenuCount, "Stored: %u / %u", (unsigned)p_view->message_count, (unsigned)SYSTEM_MSG_SLOTS);
                ui_flow_unlock();
                s_flow.shown_messages = p_view->message_count;
            }
            break;

        case SYSTEM_STATE_MSG_LIST:
        {
            /* Also when a message from the web replaced one: same count, other rows. */
            const uint32_t sig = ui_flow_msg_sig(p_view);
            if ((p_view->list_sel != s_flow.shown_sel) || (p_view->list_count != s_flow.shown_count)
                || (sig != s_flow.shown_msg_sig))
            {
                ui_flow_list_draw(p_view);
                s_flow.shown_msg_sig = sig;
            }
            break;
        }

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

/* Voice time and scan countdowns: redrawn once per second. */
static void ui_flow_refresh_countdown(const system_view_t *const p_view)
{
    uint32_t total_ms;

    switch (p_view->state)
    {
        case SYSTEM_STATE_VOICE:
            total_ms = SYSTEM_VOICE_MAX_MS;
            break;
        case SYSTEM_STATE_PLAYING:
            total_ms = ((uint32_t)p_view->play_seconds * 1000U) + 1U; /* + 1: never 0. */
            break;
        case SYSTEM_STATE_ADMIN_SCAN:
            total_ms = SYSTEM_SCAN_TIMEOUT_MS;
            break;
        default:
            return;
    }

    const uint32_t elapsed_ms = (p_view->state_ms < total_ms) ? p_view->state_ms : total_ms;
    const uint32_t remain_ms  = total_ms - elapsed_ms;
    const bool     voice      = (SYSTEM_STATE_VOICE == p_view->state) || (SYSTEM_STATE_PLAYING == p_view->state);
    const int32_t  seconds    = (int32_t)(voice ? (elapsed_ms / 1000U) : ((remain_ms + 999U) / 1000U));

    const bool scans_changed = (SYSTEM_STATE_ADMIN_SCAN == p_view->state) && (p_view->scan_count != s_flow.shown_scans);
    if (((seconds == s_flow.shown_seconds) && !scans_changed) || !ui_flow_lock())
    {
        return;
    }
    switch (p_view->state)
    {
        case SYSTEM_STATE_VOICE:
        case SYSTEM_STATE_PLAYING:
            lv_label_set_text_fmt(ui_VoiceTime,
                                  "00:%02u / 00:%02u",
                                  (unsigned)seconds,
                                  (unsigned)((SYSTEM_STATE_VOICE == p_view->state) ? (SYSTEM_VOICE_MAX_MS / 1000U)
                                                                                   : p_view->play_seconds));
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
            char owner[UI_FLOW_DETAIL_SIZE];
            ui_flow_owner_text(p_view->kind, p_view->pending_user, p_view->pending_finger, owner, sizeof(owner));
            lv_label_set_text_fmt(ui_StatusValue,
                                  "%s    Scan %u / %u",
                                  owner,
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
        case SYSTEM_NOTICE_VOICE_CANCELLED:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Recording cancelled", NULL);
            break;
        case SYSTEM_NOTICE_NO_MESSAGES:
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "No messages", "The owner left no message");
            break;
        case SYSTEM_NOTICE_MSG_SENT:
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Message sent", "The owner will hear it");
            break;
        case SYSTEM_NOTICE_MSG_SAVED:
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Message saved", "Guests can play it");
            break;
        case SYSTEM_NOTICE_MSG_TOO_SHORT:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Too short", "Speak for at least 1 s");
            break;
        case SYSTEM_NOTICE_MSG_FAILED:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Record failed", "Microphone or SD card error");
            break;
        case SYSTEM_NOTICE_MSG_UNAVAILABLE:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Not available", "No SD card for messages");
            break;
        case SYSTEM_NOTICE_MSG_FULL:
            (void)snprintf(detail, sizeof(detail), "%u messages, delete one first", (unsigned)SYSTEM_MSG_SLOTS);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Failed", detail);
            break;
        case SYSTEM_NOTICE_MSG_DELETED:
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Deleted", "Message removed");
            break;
        case SYSTEM_NOTICE_PLAY_FAILED:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Cannot play", "Message unreadable");
            break;
        case SYSTEM_NOTICE_DELETED:
            ui_flow_owner_text(p_view->kind, value, p_view->notice_value2, detail, sizeof(detail));
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Deleted", detail);
            break;
        case SYSTEM_NOTICE_EMPTY:
            if (SYSTEM_STATE_MSG_MENU == p_view->state)
            {
                ui_flow_popup(UI_SERVICE_POPUP_INFO, "Nothing to play", "No messages for guests");
                break;
            }
            (void)snprintf(detail, sizeof(detail), "No %s stored", s_kind_names[p_view->kind]);
            ui_flow_popup(UI_SERVICE_POPUP_INFO, "Nothing to delete", detail);
            break;
        case SYSTEM_NOTICE_FINGER_IN_USE:
            ui_flow_owner_text(SYSTEM_CRED_FINGER, value, p_view->notice_value2, detail, sizeof(detail));
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Already added", detail);
            break;
        case SYSTEM_NOTICE_ALL_FINGERS:
            (void)snprintf(detail, sizeof(detail), "%s has all 10 fingers", system_control_user_name(value));
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Nothing to add", detail);
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
            ui_flow_owner_text(p_view->kind, value, p_view->notice_value2, detail, sizeof(detail));
            ui_flow_popup(UI_SERVICE_POPUP_SUCCESS, "Saved", detail);
            break;
        case SYSTEM_NOTICE_FULL:
            (void)snprintf(detail,
                           sizeof(detail),
                           (SYSTEM_CRED_PIN == p_view->kind) ? "Every user has a PIN" : "No free %s slot",
                           s_kind_names[p_view->kind]);
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Failed", detail);
            break;
        case SYSTEM_NOTICE_CARD_IN_USE:
            (void)snprintf(detail, sizeof(detail), "Card of %s", system_control_user_name(value));
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Already added", detail);
            break;
        case SYSTEM_NOTICE_SCAN_MISMATCH:
            /* A new card counts as its own first scan; a finger starts again from nothing. */
            if (SYSTEM_CRED_FINGER == p_view->kind)
            {
                ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Fingers do not match", "Start again: scan 1 / 3");
            }
            else
            {
                ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Cards do not match", "Counting again with this card");
            }
            break;
        case SYSTEM_NOTICE_SCAN_BAD:
            ui_flow_popup(UI_SERVICE_POPUP_ERROR, "Finger not read", "Place it again");
            break;
        case SYSTEM_NOTICE_LOCKED:
        {
            /* Only that method is locked; the popup says which, and how long, so the others are tried instead. */
            static const char *const s_lock_titles[SYSTEM_LOCK_COUNT]
                = {"PIN locked", "Cards locked", "Fingerprint locked", "Admin login locked"};
            const unsigned long left_s = (unsigned long)p_view->notice_lock_s;
            if (SYSTEM_LOCK_FOREVER == p_view->notice_lock_s)
            {
                (void)snprintf(detail, sizeof(detail), "Ask the admin to unlock it");
            }
            else if (left_s >= 60UL)
            {
                (void)snprintf(detail,
                               sizeof(detail),
                               "%s %lu:%02lu",
                               (0U != p_view->notice_value2) ? "Too many tries, locked" : "Try again in",
                               left_s / 60UL,
                               left_s % 60UL);
            }
            else
            {
                (void)snprintf(detail,
                               sizeof(detail),
                               "%s %lu s",
                               (0U != p_view->notice_value2) ? "Too many tries, locked" : "Try again in",
                               left_s);
            }
            ui_flow_popup(UI_SERVICE_POPUP_ERROR,
                          s_lock_titles[(value < (unsigned)SYSTEM_LOCK_COUNT) ? value : 0U],
                          detail);
            break;
        }
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

/* Called under the lock. Input serves ENTER PIN and the new PIN of the admin menu. */
static void ui_flow_set_input(const system_view_t *const p_view)
{
    lv_label_set_text(ui_InputKeys, "#  OK          *  Delete");
    if (SYSTEM_STATE_PIN_ENTRY == p_view->state)
    {
        lv_label_set_text(ui_InputTitle, "ENTER PIN");
        lv_label_set_text(ui_InputPrompt, "Enter PIN or scan card / fingerprint");
        lv_label_set_text_fmt(ui_InputHintText, "Returns to home after %us", (unsigned)(SYSTEM_PIN_IDLE_MS / 1000U));
    }
    else
    {
        lv_label_set_text_fmt(ui_InputTitle, "ADD PIN - %s", system_control_user_name(p_view->pending_user));
        lv_label_set_text(ui_InputPrompt,
                          (SYSTEM_STEP_ADD_PIN == p_view->step) ? "Enter new PIN (4-8 digits)" : "Enter PIN again");
        lv_label_set_text(ui_InputHintText, "Hold * to go back");
    }
    lv_textarea_set_placeholder_text(ui_InputField, "PIN");
    lv_textarea_set_password_mode(ui_InputField, true);
    /* Home -> Input keeps the field (ui_service_show()): empty it here, or the last entry would flash until
     * ui_flow_refresh() draws this one. */
    lv_textarea_set_text(ui_InputField, "");
}

/* Called under the lock, once: a highlight behind the selected row and a scroll bar on the right of ui_MenuList. */
static void ui_flow_list_create(void)
{
    lv_obj_clear_flag(ui_MenuList, LV_OBJ_FLAG_SCROLLABLE);

    s_list_highlight = lv_obj_create(ui_MenuList);
    lv_obj_remove_style_all(s_list_highlight);
    lv_obj_set_size(s_list_highlight, 250, 24);
    lv_obj_set_align(s_list_highlight, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(s_list_highlight, 6);
    lv_obj_set_style_radius(s_list_highlight, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_list_highlight, lv_color_hex(0x2F6FA8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list_highlight, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_move_background(s_list_highlight); /* Behind the row labels. */

    s_list_track = lv_obj_create(ui_MenuList);
    lv_obj_remove_style_all(s_list_track);
    lv_obj_set_size(s_list_track, 4, UI_FLOW_SCROLL_H);
    lv_obj_set_align(s_list_track, LV_ALIGN_RIGHT_MID);
    lv_obj_set_x(s_list_track, -4);
    lv_obj_set_style_radius(s_list_track, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_list_track, lv_color_hex(0x3A4652), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list_track, LV_OPA_COVER, LV_PART_MAIN);

    s_list_thumb = lv_obj_create(s_list_track);
    lv_obj_remove_style_all(s_list_thumb);
    lv_obj_set_width(s_list_thumb, 4);
    lv_obj_set_style_radius(s_list_thumb, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_list_thumb, lv_color_hex(0xC8D2DC), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_list_thumb, LV_OPA_COVER, LV_PART_MAIN);

    ui_flow_list_show(false);
}

/* Called under the lock. */
static void ui_flow_list_show(bool show)
{
    if (NULL == s_list_highlight)
    {
        return;
    }
    if (show)
    {
        lv_obj_clear_flag(s_list_highlight, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_list_highlight, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_list_track, LV_OBJ_FLAG_HIDDEN);
    }
}

/* A window of UI_FLOW_LIST_ROWS rows that follows the highlight, and a scroll bar when the list is longer. */
static void ui_flow_list_draw(const system_view_t *const p_view)
{
    lv_obj_t *const rows[UI_FLOW_LIST_ROWS] = {ui_MenuRow1, ui_MenuRow2, ui_MenuRow3, ui_MenuRow4};
    const uint8_t   count                   = p_view->list_count;
    const uint8_t   sel                     = p_view->list_sel;
    char            text[UI_FLOW_DETAIL_SIZE];

    if (sel < s_flow.list_top)
    {
        s_flow.list_top = sel;
    }
    else if (sel >= (s_flow.list_top + UI_FLOW_LIST_ROWS))
    {
        s_flow.list_top = (uint8_t)(sel - UI_FLOW_LIST_ROWS + 1U);
    }
    if ((s_flow.list_top + UI_FLOW_LIST_ROWS) > count)
    {
        s_flow.list_top = (count > UI_FLOW_LIST_ROWS) ? (uint8_t)(count - UI_FLOW_LIST_ROWS) : 0U;
    }

    if ((NULL == s_list_highlight) || !ui_flow_lock())
    {
        return;
    }
    for (uint8_t i = 0U; i < UI_FLOW_LIST_ROWS; i++)
    {
        const uint8_t index = (uint8_t)(s_flow.list_top + i);
        text[0]             = '\0';
        if (index < count)
        {
            ui_flow_row_text(p_view, index, text, sizeof(text));
        }
        lv_label_set_text(rows[i], text);
    }
    lv_obj_set_y(s_list_highlight, UI_FLOW_ROW_TOP + (UI_FLOW_ROW_STEP * (int32_t)(sel - s_flow.list_top)));

    if (count > UI_FLOW_LIST_ROWS)
    {
        /* Thumb: the share of the list in view, at the place of the window. */
        const lv_coord_t thumb_h = (lv_coord_t)((UI_FLOW_SCROLL_H * UI_FLOW_LIST_ROWS) / count);
        const lv_coord_t thumb_y
            = (lv_coord_t)(((UI_FLOW_SCROLL_H - thumb_h) * s_flow.list_top) / (count - UI_FLOW_LIST_ROWS));
        lv_obj_set_height(s_list_thumb, thumb_h);
        lv_obj_set_y(s_list_thumb, thumb_y);
        lv_obj_clear_flag(s_list_track, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_list_track, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(ui_MenuCount, "%u / %u", (unsigned)(sel + 1U), (unsigned)count);
    ui_flow_unlock();
    s_flow.shown_sel   = sel;
    s_flow.shown_count = count;
}

/* Text of one list row, also the detail of the confirm screen. */
static void ui_flow_item_text(const system_view_t *const p_view,
                              const system_list_item_t  *p_item,
                              char                      *p_out,
                              size_t                     size)
{
    if (SYSTEM_LIST_PICK_FINGER == p_view->list_purpose)
    {
        (void)snprintf(p_out, size, "%s", system_control_finger_name(p_item->finger));
    }
    else if (SYSTEM_LIST_PICK_USER == p_view->list_purpose)
    {
        if ((SYSTEM_CRED_PIN == p_view->kind) || (0U == p_item->count))
        {
            (void)snprintf(p_out, size, "%s", system_control_user_name(p_item->user));
        }
        else
        {
            (void)snprintf(p_out,
                           size,
                           "%s   (%u %s%s)",
                           system_control_user_name(p_item->user),
                           p_item->count,
                           s_kind_names[p_view->kind],
                           (1U == p_item->count) ? "" : "s");
        }
    }
    else if (SYSTEM_CRED_CARD == p_view->kind)
    {
        (void)snprintf(p_out, size, "%s - card ..%04X", system_control_user_name(p_item->user), p_item->uid_tail);
    }
    else
    {
        ui_flow_owner_text(p_view->kind, p_item->user, p_item->finger, p_out, size);
    }
}

/* Row of ADMIN_LIST or MSG_LIST. */
static void ui_flow_row_text(const system_view_t *const p_view, uint8_t index, char *p_out, size_t size)
{
    if (SYSTEM_STATE_MSG_LIST == p_view->state)
    {
        ui_flow_msg_text(&p_view->messages[index], index, p_out, size);
    }
    else
    {
        ui_flow_item_text(p_view, &p_view->list[index], p_out, size);
    }
}

/* "1. Web   12 s   14:05 02/11": where it came from, its length, when (when the clock was set). */
static void ui_flow_msg_text(const system_message_t *p_msg, uint8_t index, char *p_out, size_t size)
{
    char when[16] = "";

    if (0U != p_msg->ts)
    {
        const time_t ts = (time_t)p_msg->ts;
        struct tm    tm_local;
        if (NULL != localtime_r(&ts, &tm_local))
        {
            (void)strftime(when, sizeof(when), "%H:%M %d/%m", &tm_local);
        }
    }
    (void)snprintf(p_out,
                   size,
                   "%u. %s   %u s   %s",
                   (unsigned)(index + 1U),
                   (SYSTEM_MSG_FROM_WEB == p_msg->source) ? "Web" : "Door",
                   (unsigned)p_msg->seconds,
                   when);
}

/* Changes when a message of the list changes. */
static uint32_t ui_flow_msg_sig(const system_view_t *const p_view)
{
    uint32_t sig = 2166136261U;

    for (uint8_t i = 0U; i < p_view->message_count; i++)
    {
        sig = (sig ^ p_view->messages[i].slot) * 16777619U;
        sig = (sig ^ p_view->messages[i].ts) * 16777619U;
        sig = (sig ^ p_view->messages[i].seconds) * 16777619U;
    }
    return sig;
}

/* "Dat - Left index", "Dat - card", "Dat - PIN". */
static void ui_flow_owner_text(system_cred_kind_t kind, uint8_t user, uint8_t finger, char *p_out, size_t size)
{
    (void)snprintf(p_out,
                   size,
                   "%s - %s",
                   system_control_user_name(user),
                   (SYSTEM_CRED_FINGER == kind) ? system_control_finger_name(finger) : s_kind_names[kind]);
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

/* The battery icon and percent in the top bar, from ui_flow_set_battery(). */
static void ui_flow_battery(void)
{
    const uint8_t percent = ui_flow_get_battery();
    if (((int16_t)percent == s_flow.battery_shown) || !ui_flow_lock())
    {
        return;
    }

    char        text[UI_FLOW_PCT_SIZE] = "--%";
    const char *p_icon                 = LV_SYMBOL_BATTERY_EMPTY;
    if (UI_FLOW_BATTERY_UNKNOWN != percent)
    {
        (void)snprintf(text, sizeof(text), "%u%%", (unsigned)percent);
        p_icon = (percent >= 90U)   ? LV_SYMBOL_BATTERY_FULL
                 : (percent >= 65U) ? LV_SYMBOL_BATTERY_3
                 : (percent >= 40U) ? LV_SYMBOL_BATTERY_2
                 : (percent >= 15U) ? LV_SYMBOL_BATTERY_1
                                    : LV_SYMBOL_BATTERY_EMPTY;
    }
    lv_label_set_text(ui_TopBarBattery, p_icon);
    lv_label_set_text(ui_TopBarBatteryPct, text);
    ui_flow_unlock();
    s_flow.battery_shown = (int16_t)percent;
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

/***********************************************************************************************************************
 * Battery value, also without a screen. One byte written by the battery code and read by the flow task: no lock.
 **********************************************************************************************************************/

static volatile uint8_t s_battery_percent = UI_FLOW_BATTERY_UNKNOWN;

void ui_flow_set_battery(uint8_t percent)
{
    s_battery_percent = ((UI_FLOW_BATTERY_UNKNOWN != percent) && (percent > 100U)) ? 100U : percent;
}

uint8_t ui_flow_get_battery(void)
{
    return s_battery_percent;
}
