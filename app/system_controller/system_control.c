#include "system_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define SYSTEM_TAG           "SYSTEM_CONTROL"
#define SYSTEM_LOCK_WAIT_MS  100U
#define SYSTEM_SLOT_NONE     0xFFU
#define SYSTEM_PLAY_SPARE_MS 5000U /* PLAYING ends this long after the message should have, without a report. */

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Hook calls decided under the lock and made after it, so a slow hook never holds up the keypad or the screen. */
typedef struct st_system_effects
{
    bool                 access;
    system_method_t      access_method;
    bool                 access_granted;
    uint8_t              access_user;
    bool                 alert;
    system_alert_t       alert_code;
    system_lock_target_t alert_target;
    uint32_t             alert_lock_s;
    bool                 locks_forever; /* The targets locked for good changed: tell on_locks_forever. */
    uint8_t              locks_mask;
    bool                 door;
    bool                 door_unlocked;
    bool                 voice;
    system_voice_t       voice_action;
    bool                 voice_owner;
    bool                 play;
    uint8_t              play_slot;
    uint8_t              play_token;
    bool                 play_stop;
    uint8_t              message_delete; /* Slot + 1 of the message for guests that goes, or 0. */
    uint8_t              finger_delete;  /* Sensor page whose template goes, or 0. */
} system_effects_t;

/* Wrong tries and lockouts of one system_lock_target_t. */
typedef struct st_system_lock
{
    uint8_t    fails;    /* Wrong tries since its last lockout or the last good unlock. */
    uint8_t    lockouts; /* Lockouts in a row: picks the next length; past the last step it is for good. */
    bool       forever;  /* Only an admin login ends it. */
    uint32_t   lock_ms;  /* Length of the running lockout, 0 when none. */
    TickType_t lock_tick;
} system_lock_t;

typedef struct st_system_ctx
{
    SemaphoreHandle_t      mutex;
    system_control_hooks_t hooks;
    system_state_t         state;
    system_cred_kind_t     kind;
    system_admin_step_t    step;
    system_list_purpose_t  list_purpose;
    uint8_t                list_count;
    uint8_t                list_sel;
    system_list_item_t     list[SYSTEM_LIST_MAX];
    bool                   login_on_password;
    char                   entry[SYSTEM_ENTRY_MAX + 1U]; /* The field that has the keys. */
    uint8_t                entry_len;
    char                   login_id[SYSTEM_ENTRY_MAX + 1U];
    uint8_t                pending_user;                       /* Admin add / delete: the user picked. */
    uint8_t                pending_finger;                     /* Admin add / delete of a finger: the finger picked. */
    uint8_t                pending_slot;                       /* Admin delete of a card / finger: its slot. */
    char                   pending_pin[SYSTEM_ENTRY_MAX + 1U]; /* Admin add: first entry of the new PIN. */
    TickType_t             state_tick;
    TickType_t             activity_tick;
    TickType_t             unlock_tick;
    TickType_t             card_tick;
    uint8_t                last_uid[SYSTEM_UID_MAX];
    uint8_t                last_uid_len;
    uint8_t                scan_slot;                /* ADMIN_SCAN: slot the card / finger goes to. */
    uint8_t                scan_count;               /* ADMIN_SCAN: good scans so far. */
    uint8_t                scan_uid[SYSTEM_UID_MAX]; /* ADMIN_SCAN: card of the first scan. */
    uint8_t                scan_uid_len;
    bool                   door_unlocked;
    system_lock_t          locks[SYSTEM_LOCK_COUNT];
    uint32_t               notice_lock_s; /* NOTICE_LOCKED: seconds left, or SYSTEM_LOCK_FOREVER. */
    uint8_t                message_count;
    system_message_t       messages[SYSTEM_MSG_SLOTS]; /* The owner's messages for guests, newest first. */
    bool                   voice_ready;                /* Messages can be kept (SD card). */
    bool                   msg_admin;                  /* VOICE, MSG_LIST, PLAYING: for the admin. */
    system_state_t         play_from;                  /* PLAYING: back to HOME, MSG_LIST or VOICE_REVIEW. */
    uint8_t                review_seconds;
    uint8_t                play_seconds;
    uint8_t                play_token; /* New for every message played. */
    uint32_t               notice_seq;
    system_notice_t        notice;
    uint8_t                notice_value;
    uint8_t                notice_value2;
    uint32_t               cred_seq; /* Bumped on every credential change, for storage. */
    bool                   init;
} system_ctx_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static bool     system_lock(void);
static void     system_unlock_and_apply(const system_effects_t *const p_fx);
static void     system_enter(system_state_t state);
static void     system_notify(system_notice_t notice, uint8_t value);
static void     system_notify2(system_notice_t notice, uint8_t value, uint8_t value2);
static void     system_entry_clear(void);
static void     system_entry_edit(char key);
static void     system_go_home(void);
static void     system_voice_start(bool owner, system_effects_t *const p_fx);
static void     system_voice_leave(void);
static void     system_voice_review(system_effects_t *const p_fx, system_voice_t action);
static void     system_review_key(char key, bool hold, system_effects_t *const p_fx);
static void     system_msg_list_open(bool admin);
static void     system_msg_list_key(char key, system_effects_t *const p_fx);
static void     system_play(uint8_t index, system_state_t from, system_effects_t *const p_fx);
static void     system_play_leave(void);
static uint8_t  system_msg_index(uint8_t slot);
static void     system_handle_key(char key, bool hold, system_effects_t *const p_fx);
static bool     system_has_hold(char key);
static void     system_admin_login_key(char key);
static void     system_check_pin(system_effects_t *const p_fx);
static void     system_deny(system_method_t method, system_effects_t *const p_fx);
static void     system_check_admin(system_effects_t *const p_fx);
static void     system_admin_submit(void);
static void     system_go_cred_add(void);
static void     system_go_delete_list(void);
static void     system_list_build(system_list_purpose_t purpose);
static void     system_list_key(char key);
static void     system_scan_start(void);
static void     system_delete_pending(system_effects_t *const p_fx);
static bool     system_scan_good(void);
static void     system_grant(system_method_t method, uint8_t user, system_effects_t *const p_fx);
static bool     system_fail(system_lock_target_t target, system_effects_t *const p_fx);
static void     system_lockout(system_lock_target_t target, system_effects_t *const p_fx);
static bool     system_locked(system_lock_target_t target);
static uint32_t system_lock_left_s(system_lock_target_t target);
static uint8_t  system_forever_mask(void);
static uint32_t system_elapsed_ms(TickType_t since, TickType_t now);
static bool     system_text_equal(const char *p_a, const char *p_b);
static bool     system_pin_valid(const char *p_pin);
static uint8_t  system_find_pin(const char *p_pin);
static uint8_t  system_find_card(const uint8_t *p_uid, uint8_t uid_length);
static uint8_t  system_free_slot(system_cred_kind_t kind);
static uint8_t  system_count_of(system_cred_kind_t kind, uint8_t user);
static uint8_t  system_user_count(system_cred_kind_t kind);
static bool     system_has_finger(uint8_t user, uint8_t finger);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static system_ctx_t s_sys = {0};

/* Credentials. storage_task keeps them on the SD card; without one they live in RAM only. */
static char    s_pins[SYSTEM_USER_COUNT][SYSTEM_ENTRY_MAX + 1U]; /* By user ID - 1. */
static uint8_t s_card_user[SYSTEM_CARD_SLOTS];                   /* By slot. 0: free. */
static uint8_t s_cards[SYSTEM_CARD_SLOTS][SYSTEM_UID_MAX];
static uint8_t s_card_len[SYSTEM_CARD_SLOTS];
static uint8_t s_finger_user[SYSTEM_FINGER_SLOTS]; /* By slot = sensor page - 1. 0: free. */
static uint8_t s_finger_id[SYSTEM_FINGER_SLOTS];

/* Users of this project; the others are numbered. */
static const char *const s_user_names[] = {"Dat", "Phi", "Tuan"};

static const char *const s_finger_names[SYSTEM_FINGER_COUNT + 1U] = {"Left thumb",
                                                                     "Left index",
                                                                     "Left middle",
                                                                     "Left ring",
                                                                     "Left little",
                                                                     "Right thumb",
                                                                     "Right index",
                                                                     "Right middle",
                                                                     "Right ring",
                                                                     "Right little",
                                                                     "Finger"};

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

app_err_t system_control_init(const system_control_hooks_t *const p_hooks)
{
    if (s_sys.init)
    {
        return APP_ERR_INVALID_STATE;
    }

    s_sys.mutex = xSemaphoreCreateMutex();
    if (NULL == s_sys.mutex)
    {
        return APP_ERR_INVALID_STATE;
    }
    if (NULL != p_hooks)
    {
        s_sys.hooks = *p_hooks;
    }
    (void)snprintf(s_pins[0], sizeof(s_pins[0]), "%s", SYSTEM_DEFAULT_PIN);
    (void)system_control_user_name(0U); /* Builds the name table before any other task asks. */
    system_go_home();
    s_sys.init = true;
    return APP_SUCCESS;
}

void system_control_key(char key, bool hold)
{
    system_effects_t fx = {0};

    if (!system_lock())
    {
        return;
    }
    s_sys.activity_tick = xTaskGetTickCount();
    system_handle_key(key, hold, &fx);
    system_unlock_and_apply(&fx);
}

void system_control_card(const uint8_t *p_uid, uint8_t uid_length)
{
    system_effects_t fx = {0};

    if ((NULL == p_uid) || (0U == uid_length) || (uid_length > SYSTEM_UID_MAX) || !system_lock())
    {
        return;
    }

    const TickType_t now    = xTaskGetTickCount();
    const bool       same   = (uid_length == s_sys.last_uid_len) && (0 == memcmp(p_uid, s_sys.last_uid, uid_length));
    const bool       repeat = same && (system_elapsed_ms(s_sys.card_tick, now) < SYSTEM_CARD_REPEAT_MS);

    s_sys.card_tick = now;
    memcpy(s_sys.last_uid, p_uid, uid_length);
    s_sys.last_uid_len = uid_length;
    if (repeat)
    {
        system_unlock_and_apply(&fx);
        return;
    }

    const uint8_t user = system_find_card(p_uid, uid_length);
    switch (s_sys.state)
    {
        case SYSTEM_STATE_HOME:
        case SYSTEM_STATE_PIN_ENTRY:
            /* Locked cards: even a registered one is refused, so the lockout means the same for everyone. */
            if (system_locked(SYSTEM_LOCK_CARD))
            {
                break;
            }
            if (0U != user)
            {
                ESP_LOGI(SYSTEM_TAG, "Card accepted, user %u", user);
                system_grant(SYSTEM_METHOD_CARD, user, &fx);
            }
            else
            {
                fx.alert      = true;
                fx.alert_code = SYSTEM_ALERT_UNKNOWN_CARD;
                system_deny(SYSTEM_METHOD_CARD, &fx);
            }
            break;

        case SYSTEM_STATE_ADMIN_SCAN:
            if (SYSTEM_CRED_CARD != s_sys.kind)
            {
                break;
            }
            s_sys.activity_tick = now;
            if (0U != user)
            {
                system_enter(SYSTEM_STATE_CRED_MENU);
                system_notify(SYSTEM_NOTICE_CARD_IN_USE, user);
                break;
            }
            if ((0U != s_sys.scan_count)
                && ((uid_length != s_sys.scan_uid_len) || (0 != memcmp(p_uid, s_sys.scan_uid, uid_length))))
            {
                /* Not the card of the first scan: start again with this one. */
                s_sys.scan_count = 0U;
                system_notify(SYSTEM_NOTICE_SCAN_MISMATCH, 0U);
            }
            if (0U == s_sys.scan_count)
            {
                memcpy(s_sys.scan_uid, p_uid, uid_length);
                s_sys.scan_uid_len = uid_length;
            }
            if (system_scan_good())
            {
                s_card_user[s_sys.scan_slot] = s_sys.pending_user;
                memcpy(s_cards[s_sys.scan_slot], p_uid, uid_length);
                s_card_len[s_sys.scan_slot] = uid_length;
                s_sys.cred_seq++;
                ESP_LOGI(SYSTEM_TAG, "Card added for user %u (slot %u)", s_sys.pending_user, s_sys.scan_slot);
            }
            break;

        default:
            break;
    }
    system_unlock_and_apply(&fx);
}

void system_control_finger(uint8_t page)
{
    system_effects_t fx = {0};

    if ((page > SYSTEM_FINGER_SLOTS) || !system_lock())
    {
        return;
    }
    const uint8_t user = (0U != page) ? s_finger_user[page - 1U] : 0U;

    switch (s_sys.state)
    {
        case SYSTEM_STATE_HOME:
        case SYSTEM_STATE_PIN_ENTRY:
            if (system_locked(SYSTEM_LOCK_FINGER))
            {
                break;
            }
            if (0U != user)
            {
                ESP_LOGI(SYSTEM_TAG, "Finger accepted, user %u (%s)", user, s_finger_names[s_finger_id[page - 1U]]);
                system_grant(SYSTEM_METHOD_FINGER, user, &fx);
            }
            else
            {
                system_deny(SYSTEM_METHOD_FINGER, &fx);
            }
            break;

        default: /* Enrolling goes through system_control_finger_scanned(). */
            break;
    }
    system_unlock_and_apply(&fx);
}

void system_control_finger_scanned(bool good)
{
    system_effects_t fx = {0};

    if (!system_lock())
    {
        return;
    }
    if ((SYSTEM_STATE_ADMIN_SCAN == s_sys.state) && (SYSTEM_CRED_FINGER == s_sys.kind))
    {
        s_sys.activity_tick = xTaskGetTickCount();
        if (!good)
        {
            system_notify(SYSTEM_NOTICE_SCAN_BAD, 0U);
        }
        else if (system_scan_good())
        {
            s_finger_user[s_sys.scan_slot] = s_sys.pending_user;
            s_finger_id[s_sys.scan_slot]   = s_sys.pending_finger;
            s_sys.cred_seq++;
            ESP_LOGI(SYSTEM_TAG,
                     "Fingerprint added for user %u: %s (page %u)",
                     s_sys.pending_user,
                     s_finger_names[s_sys.pending_finger],
                     s_sys.scan_slot + 1U);
        }
    }
    system_unlock_and_apply(&fx);
}

bool system_control_finger_scan_duplicate(uint8_t page)
{
    system_effects_t fx    = {0};
    bool             owned = false;

    if ((0U == page) || (page > SYSTEM_FINGER_SLOTS) || !system_lock())
    {
        return false;
    }
    owned = (0U != s_finger_user[page - 1U]);
    if (owned && (SYSTEM_STATE_ADMIN_SCAN == s_sys.state) && (SYSTEM_CRED_FINGER == s_sys.kind))
    {
        /* One finger, one owner: otherwise the sensor would always answer with the older template. */
        ESP_LOGW(SYSTEM_TAG, "Finger already stored at page %u", page);
        system_enter(SYSTEM_STATE_CRED_MENU);
        system_notify2(SYSTEM_NOTICE_FINGER_IN_USE, s_finger_user[page - 1U], s_finger_id[page - 1U]);
    }
    system_unlock_and_apply(&fx);
    return owned;
}

void system_control_finger_scan_restart(void)
{
    system_effects_t fx = {0};

    if (!system_lock())
    {
        return;
    }
    if ((SYSTEM_STATE_ADMIN_SCAN == s_sys.state) && (SYSTEM_CRED_FINGER == s_sys.kind))
    {
        s_sys.activity_tick = xTaskGetTickCount();
        s_sys.state_tick    = s_sys.activity_tick;
        s_sys.scan_count    = 0U;
        system_notify(SYSTEM_NOTICE_SCAN_MISMATCH, 0U);
    }
    system_unlock_and_apply(&fx);
}

void system_control_tick(void)
{
    system_effects_t fx = {0};

    if (!system_lock())
    {
        return;
    }

    const TickType_t now      = xTaskGetTickCount();
    const uint32_t   state_ms = system_elapsed_ms(s_sys.state_tick, now);
    const uint32_t   idle_ms  = system_elapsed_ms(s_sys.activity_tick, now);

    if (s_sys.door_unlocked && (system_elapsed_ms(s_sys.unlock_tick, now) >= SYSTEM_UNLOCK_MS))
    {
        s_sys.door_unlocked = false;
        fx.door             = true;
        fx.door_unlocked    = false;
    }

    for (uint8_t i = 0U; i < (uint8_t)SYSTEM_LOCK_COUNT; i++)
    {
        system_lock_t *const p_lock = &s_sys.locks[i];
        if ((0U != p_lock->lock_ms) && (system_elapsed_ms(p_lock->lock_tick, now) >= p_lock->lock_ms))
        {
            /* Its tries were reset when it locked; the count of lockouts stays, so the next one lasts longer. */
            ESP_LOGI(SYSTEM_TAG, "Lockout %u ended", i);
            p_lock->lock_ms = 0U;
        }
    }

    switch (s_sys.state)
    {
        case SYSTEM_STATE_PIN_ENTRY:
            if (idle_ms >= SYSTEM_PIN_IDLE_MS)
            {
                system_go_home();
            }
            break;

        case SYSTEM_STATE_ADMIN_LOGIN:
            if (idle_ms >= SYSTEM_LOGIN_IDLE_MS)
            {
                system_go_home();
            }
            break;

        case SYSTEM_STATE_ADMIN_MENU:
        case SYSTEM_STATE_CRED_MENU:
        case SYSTEM_STATE_ADMIN_INPUT:
        case SYSTEM_STATE_ADMIN_LIST:
        case SYSTEM_STATE_ADMIN_CONFIRM:
        case SYSTEM_STATE_MSG_MENU:
        case SYSTEM_STATE_MSG_DELETE:
            if (idle_ms >= SYSTEM_ADMIN_IDLE_MS)
            {
                ESP_LOGI(SYSTEM_TAG, "Admin mode timed out");
                system_go_home();
                system_notify(SYSTEM_NOTICE_ADMIN_TIMEOUT, 0U);
            }
            break;

        case SYSTEM_STATE_VOICE:
            if (state_ms >= SYSTEM_VOICE_MAX_MS)
            {
                system_voice_review(&fx, SYSTEM_VOICE_STOP);
            }
            break;

        case SYSTEM_STATE_VOICE_REVIEW:
            if (idle_ms >= SYSTEM_REVIEW_IDLE_MS)
            {
                /* The guest walked away: what was recorded is more likely meant to go than to be lost. */
                ESP_LOGI(SYSTEM_TAG, "Review timed out, message kept");
                fx.voice        = true;
                fx.voice_action = SYSTEM_VOICE_SEND;
                fx.voice_owner  = s_sys.msg_admin;
                system_voice_leave();
            }
            break;

        case SYSTEM_STATE_MSG_LIST:
            if (idle_ms >= (s_sys.msg_admin ? SYSTEM_ADMIN_IDLE_MS : SYSTEM_MSG_IDLE_MS))
            {
                system_go_home();
                if (s_sys.msg_admin)
                {
                    ESP_LOGI(SYSTEM_TAG, "Admin mode timed out");
                    system_notify(SYSTEM_NOTICE_ADMIN_TIMEOUT, 0U);
                }
            }
            break;

        case SYSTEM_STATE_PLAYING:
            /* The voice code reports the end; this only covers a report that never comes. */
            if (state_ms >= (((uint32_t)s_sys.play_seconds * 1000U) + SYSTEM_PLAY_SPARE_MS))
            {
                fx.play_stop = true;
                system_play_leave();
            }
            break;

        case SYSTEM_STATE_ADMIN_SCAN:
            if (state_ms >= SYSTEM_SCAN_TIMEOUT_MS)
            {
                system_enter(SYSTEM_STATE_CRED_MENU);
                system_notify(SYSTEM_NOTICE_SCAN_TIMEOUT, 0U);
            }
            break;

        default:
            break;
    }
    system_unlock_and_apply(&fx);
}

void system_control_set_locks_forever(uint8_t mask)
{
    if (!system_lock())
    {
        return;
    }
    for (uint8_t i = 0U; i < (uint8_t)SYSTEM_LOCK_ADMIN; i++) /* The admin login is never locked for good. */
    {
        s_sys.locks[i].forever = (0U != (mask & (1U << i)));
        if (s_sys.locks[i].forever)
        {
            ESP_LOGW(SYSTEM_TAG, "Lockout %u kept from before the restart, until an admin login", i);
        }
    }
    xSemaphoreGive(s_sys.mutex);
}

void system_control_set_messages(bool ready, const system_message_t *p_list, uint8_t count)
{
    if ((count > SYSTEM_MSG_SLOTS) || ((NULL == p_list) && (0U != count)) || !system_lock())
    {
        return;
    }
    s_sys.voice_ready   = ready;
    s_sys.message_count = count;
    memset(s_sys.messages, 0, sizeof(s_sys.messages));
    if (0U != count)
    {
        memcpy(s_sys.messages, p_list, count * sizeof(s_sys.messages[0]));
    }

    /* A message arrived from the web, or went: keep the list on screen in step. */
    if ((SYSTEM_STATE_MSG_LIST == s_sys.state) || (SYSTEM_STATE_MSG_DELETE == s_sys.state))
    {
        s_sys.list_count = count;
        if (0U == count)
        {
            system_msg_list_open(s_sys.msg_admin); /* Leaves the list. */
        }
        else if (s_sys.list_sel >= count)
        {
            s_sys.list_sel = (uint8_t)(count - 1U);
        }
        if ((SYSTEM_STATE_MSG_DELETE == s_sys.state) && (0U != count))
        {
            /* The rows may have moved: confirm the same message, or go back if it is gone. */
            const uint8_t index = system_msg_index(s_sys.pending_slot);
            if (index < count)
            {
                s_sys.list_sel = index;
            }
            else
            {
                system_enter(SYSTEM_STATE_MSG_LIST);
            }
        }
    }
    xSemaphoreGive(s_sys.mutex);
}

void system_control_voice_result(system_notice_t notice)
{
    if (!system_lock())
    {
        return;
    }
    const bool failed = (SYSTEM_NOTICE_MSG_SENT != notice) && (SYSTEM_NOTICE_MSG_SAVED != notice);
    if (failed && ((SYSTEM_STATE_VOICE == s_sys.state) || (SYSTEM_STATE_VOICE_REVIEW == s_sys.state)))
    {
        system_voice_leave(); /* Nothing to review: could not record, or too short. */
    }
    system_notify(notice, 0U);
    xSemaphoreGive(s_sys.mutex);
}

void system_control_voice_recorded(uint8_t seconds)
{
    if (!system_lock())
    {
        return;
    }
    s_sys.review_seconds = (0U != seconds) ? seconds : 1U;
    xSemaphoreGive(s_sys.mutex);
}

void system_control_play_done(uint8_t token, bool ok)
{
    if (!system_lock())
    {
        return;
    }
    if ((SYSTEM_STATE_PLAYING == s_sys.state) && (token == s_sys.play_token))
    {
        system_play_leave();
        if (!ok)
        {
            system_notify(SYSTEM_NOTICE_PLAY_FAILED, 0U);
        }
    }
    xSemaphoreGive(s_sys.mutex);
}

bool system_control_get_view(system_view_t *const p_view)
{
    if ((NULL == p_view) || !s_sys.init || (pdTRUE != xSemaphoreTake(s_sys.mutex, pdMS_TO_TICKS(SYSTEM_LOCK_WAIT_MS))))
    {
        return false;
    }

    const bool shows_id = (SYSTEM_STATE_ADMIN_LOGIN == s_sys.state) && !s_sys.login_on_password;

    memset(p_view, 0, sizeof(*p_view));
    p_view->state             = s_sys.state;
    p_view->kind              = s_sys.kind;
    p_view->step              = s_sys.step;
    p_view->door_unlocked     = s_sys.door_unlocked;
    p_view->login_on_password = s_sys.login_on_password;
    p_view->entry_len         = s_sys.entry_len;
    if (shows_id)
    {
        memcpy(p_view->entry_text, s_sys.entry, sizeof(p_view->entry_text));
    }
    memcpy(p_view->login_id, s_sys.login_id, sizeof(p_view->login_id));
    p_view->stored_count   = system_user_count(s_sys.kind);
    p_view->message_count  = s_sys.message_count;
    p_view->msg_admin      = s_sys.msg_admin;
    p_view->play_seconds   = s_sys.play_seconds;
    p_view->review_seconds = s_sys.review_seconds;
    p_view->state_ms       = system_elapsed_ms(s_sys.state_tick, xTaskGetTickCount());
    p_view->notice_seq     = s_sys.notice_seq;
    p_view->notice         = s_sys.notice;
    p_view->notice_value   = s_sys.notice_value;
    p_view->notice_value2  = s_sys.notice_value2;
    p_view->notice_lock_s  = s_sys.notice_lock_s;
    p_view->pending_user   = s_sys.pending_user;
    p_view->pending_finger = s_sys.pending_finger;
    p_view->scan_slot      = s_sys.scan_slot;
    p_view->scan_count     = s_sys.scan_count;
    p_view->list_purpose   = s_sys.list_purpose;
    p_view->list_count     = s_sys.list_count;
    p_view->list_sel       = s_sys.list_sel;
    memcpy(p_view->list, s_sys.list, sizeof(p_view->list));
    memcpy(p_view->messages, s_sys.messages, sizeof(p_view->messages));
    xSemaphoreGive(s_sys.mutex);
    return true;
}

uint32_t system_control_credentials_seq(void)
{
    /* One word read: no lock needed for a hint. */
    return s_sys.cred_seq;
}

bool system_control_get_credentials(system_credentials_t *const p_cred, uint32_t *const p_seq)
{
    if ((NULL == p_cred) || !s_sys.init || (pdTRUE != xSemaphoreTake(s_sys.mutex, pdMS_TO_TICKS(SYSTEM_LOCK_WAIT_MS))))
    {
        return false;
    }

    memcpy(p_cred->pins, s_pins, sizeof(p_cred->pins));
    memcpy(p_cred->card_user, s_card_user, sizeof(p_cred->card_user));
    memcpy(p_cred->cards, s_cards, sizeof(p_cred->cards));
    memcpy(p_cred->card_len, s_card_len, sizeof(p_cred->card_len));
    memcpy(p_cred->finger_user, s_finger_user, sizeof(p_cred->finger_user));
    memcpy(p_cred->finger_id, s_finger_id, sizeof(p_cred->finger_id));
    if (NULL != p_seq)
    {
        *p_seq = s_sys.cred_seq;
    }
    xSemaphoreGive(s_sys.mutex);
    return true;
}

app_err_t system_control_set_credentials(const system_credentials_t *const p_cred)
{
    APP_ASSERT(NULL != p_cred);
    if (!system_lock())
    {
        return APP_ERR_TIMEOUT;
    }

    memset(s_pins, 0, sizeof(s_pins));
    memset(s_card_user, 0, sizeof(s_card_user));
    memset(s_cards, 0, sizeof(s_cards));
    memset(s_card_len, 0, sizeof(s_card_len));
    memset(s_finger_user, 0, sizeof(s_finger_user));
    memset(s_finger_id, 0, sizeof(s_finger_id));

    /* Taken one by one so system_find_pin / system_find_card / system_has_finger see only what was already accepted. */
    for (uint8_t i = 0U; i < SYSTEM_USER_COUNT; i++)
    {
        if ('\0' == p_cred->pins[i][0])
        {
            continue;
        }
        if (!system_pin_valid(p_cred->pins[i]) || (0U != system_find_pin(p_cred->pins[i])))
        {
            ESP_LOGW(SYSTEM_TAG, "Stored PIN of user %u dropped: invalid or used by another user", i + 1U);
        }
        else
        {
            memcpy(s_pins[i], p_cred->pins[i], sizeof(s_pins[0]));
        }
    }
    for (uint8_t i = 0U; i < SYSTEM_CARD_SLOTS; i++)
    {
        const uint8_t user = p_cred->card_user[i];
        const uint8_t len  = p_cred->card_len[i];
        if (0U == user)
        {
            continue;
        }
        if ((user > SYSTEM_USER_COUNT) || (0U == len) || (len > SYSTEM_UID_MAX)
            || (0U != system_find_card(p_cred->cards[i], len)))
        {
            ESP_LOGW(SYSTEM_TAG, "Stored card in slot %u dropped: invalid or already stored", i);
            continue;
        }
        s_card_user[i] = user;
        memcpy(s_cards[i], p_cred->cards[i], len);
        s_card_len[i] = len;
    }
    for (uint8_t i = 0U; i < SYSTEM_FINGER_SLOTS; i++)
    {
        const uint8_t user   = p_cred->finger_user[i];
        const uint8_t finger = p_cred->finger_id[i];
        if (0U == user)
        {
            continue;
        }
        if ((user > SYSTEM_USER_COUNT) || (finger > SYSTEM_FINGER_UNKNOWN)
            || ((SYSTEM_FINGER_UNKNOWN != finger) && system_has_finger(user, finger)))
        {
            ESP_LOGW(SYSTEM_TAG, "Stored finger in slot %u dropped: invalid or already stored", i);
            continue;
        }
        s_finger_user[i] = user;
        s_finger_id[i]   = finger;
    }
    xSemaphoreGive(s_sys.mutex);
    return APP_SUCCESS;
}

const char *system_control_user_name(uint8_t user)
{
    /* Built once, read-only after: safe from any task. */
    static char s_numbered[SYSTEM_USER_COUNT + 1U][10];
    static bool s_built = false;

    if (!s_built)
    {
        for (uint8_t i = 0U; i <= SYSTEM_USER_COUNT; i++)
        {
            (void)snprintf(s_numbered[i], sizeof(s_numbered[i]), "User %u", i);
        }
        s_built = true;
    }
    if (SYSTEM_USER_TEMP == user)
    {
        return "Temp code"; /* ASCII: the screen fonts have no Vietnamese; the web and Telegram translate it. */
    }
    if ((user >= 1U) && (user <= (sizeof(s_user_names) / sizeof(s_user_names[0]))))
    {
        return s_user_names[user - 1U];
    }
    return s_numbered[(user <= SYSTEM_USER_COUNT) ? user : 0U];
}

const char *system_control_finger_name(uint8_t finger)
{
    return s_finger_names[(finger <= SYSTEM_FINGER_UNKNOWN) ? finger : SYSTEM_FINGER_UNKNOWN];
}

/***********************************************************************************************************************
 * Private APIs: all called with the lock held.
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * What each key does on each screen (docs/ui_data.md). A press acts when the key is released, so the same key can
 * also be held: 0 is a digit, held it opens admin login.
 **********************************************************************************************************************/
static void system_handle_key(char key, bool hold, system_effects_t *const p_fx)
{
    const bool digit = (key >= '0') && (key <= '9');

    if (hold && !system_has_hold(key))
    {
        hold = false; /* A key held too long where holding it means nothing is still typed, not lost. */
    }

    switch (s_sys.state)
    {
        case SYSTEM_STATE_HOME:
            if (hold && ('*' == key))
            {
                system_voice_start(false, p_fx); /* A message for the owner. */
            }
            else if (hold && ('#' == key))
            {
                /* Only the owner's messages: a guest never hears what another guest left. */
                if (0U == s_sys.message_count)
                {
                    system_notify(SYSTEM_NOTICE_NO_MESSAGES, 0U);
                }
                else if (1U == s_sys.message_count)
                {
                    system_play(0U, SYSTEM_STATE_HOME, p_fx);
                }
                else
                {
                    system_msg_list_open(false);
                }
            }
            else if (hold && ('0' == key))
            {
                if (!system_locked(SYSTEM_LOCK_ADMIN))
                {
                    system_enter(SYSTEM_STATE_ADMIN_LOGIN);
                }
            }
            else if (!hold && digit)
            {
                if (!system_locked(SYSTEM_LOCK_PIN))
                {
                    system_enter(SYSTEM_STATE_PIN_ENTRY);
                    system_entry_edit(key);
                }
            }
            break;

        case SYSTEM_STATE_PIN_ENTRY:
            if (hold && ('*' == key))
            {
                system_go_home();
            }
            else if (hold && ('0' == key))
            {
                if (!system_locked(SYSTEM_LOCK_ADMIN))
                {
                    system_enter(SYSTEM_STATE_ADMIN_LOGIN);
                }
            }
            else if (!hold && ('#' == key))
            {
                if (0U != s_sys.entry_len)
                {
                    system_check_pin(p_fx);
                }
            }
            else if (!hold)
            {
                system_entry_edit(key);
            }
            break;

        case SYSTEM_STATE_VOICE:
            if (!hold && ('#' == key))
            {
                ESP_LOGI(SYSTEM_TAG, "Voice recording stopped");
                system_voice_review(p_fx, SYSTEM_VOICE_STOP);
            }
            else if (hold && ('*' == key))
            {
                ESP_LOGI(SYSTEM_TAG, "Voice recording cancelled");
                p_fx->voice        = true;
                p_fx->voice_action = SYSTEM_VOICE_CANCEL;
                p_fx->voice_owner  = s_sys.msg_admin;
                system_voice_leave();
                system_notify(SYSTEM_NOTICE_VOICE_CANCELLED, 0U);
            }
            break;

        case SYSTEM_STATE_VOICE_REVIEW:
            system_review_key(key, hold, p_fx);
            break;

        case SYSTEM_STATE_MSG_LIST:
            system_msg_list_key(key, p_fx);
            break;

        case SYSTEM_STATE_PLAYING:
            if (('#' == key) || ('*' == key))
            {
                p_fx->play_stop = true;
                system_play_leave();
            }
            break;

        case SYSTEM_STATE_MSG_MENU:
            if (!hold && ('1' == key))
            {
                if (s_sys.message_count >= SYSTEM_MSG_SLOTS)
                {
                    system_notify(SYSTEM_NOTICE_MSG_FULL, 0U);
                }
                else
                {
                    system_voice_start(true, p_fx);
                }
            }
            else if (!hold && ('2' == key))
            {
                system_msg_list_open(true);
            }
            else if (!hold && ('0' == key))
            {
                system_enter(SYSTEM_STATE_ADMIN_MENU);
            }
            break;

        case SYSTEM_STATE_MSG_DELETE:
            if (!hold && ('#' == key))
            {
                const uint8_t index = system_msg_index(s_sys.pending_slot);
                if (index >= s_sys.message_count)
                {
                    system_enter(SYSTEM_STATE_MSG_LIST);
                    break;
                }
                p_fx->message_delete = (uint8_t)(s_sys.messages[index].slot + 1U);
                ESP_LOGI(SYSTEM_TAG, "Message for guests deleted (slot %u)", s_sys.messages[index].slot);
                /* Gone from the list now; the voice code deletes the file and sends the list again. */
                memmove(&s_sys.messages[index],
                        &s_sys.messages[index + 1U],
                        (size_t)(SYSTEM_MSG_SLOTS - index - 1U) * sizeof(s_sys.messages[0]));
                memset(&s_sys.messages[SYSTEM_MSG_SLOTS - 1U], 0, sizeof(s_sys.messages[0]));
                s_sys.message_count--;
                system_msg_list_open(true);
                system_notify(SYSTEM_NOTICE_MSG_DELETED, 0U);
            }
            else if ('*' == key)
            {
                const uint8_t sel = s_sys.list_sel;
                system_enter(SYSTEM_STATE_MSG_LIST); /* No: back to the list, same row. */
                s_sys.list_sel = sel;
            }
            break;

        case SYSTEM_STATE_ADMIN_LOGIN:
            if (hold && ('*' == key))
            {
                system_go_home();
            }
            else if (!hold && ('#' == key) && s_sys.login_on_password)
            {
                if (0U != s_sys.entry_len)
                {
                    system_check_admin(p_fx);
                }
            }
            else if (!hold)
            {
                system_admin_login_key(key);
            }
            break;

        case SYSTEM_STATE_ADMIN_MENU:
            if (!hold && (key >= '1') && (key <= '3'))
            {
                s_sys.kind = (system_cred_kind_t)(key - '1');
                system_enter(SYSTEM_STATE_CRED_MENU);
            }
            else if (!hold && ('4' == key))
            {
                system_enter(SYSTEM_STATE_MSG_MENU);
            }
            else if (!hold && ('0' == key))
            {
                ESP_LOGI(SYSTEM_TAG, "Admin logout");
                system_go_home();
            }
            break;

        case SYSTEM_STATE_CRED_MENU:
            if (!hold && ('1' == key))
            {
                system_go_cred_add();
            }
            else if (!hold && ('2' == key))
            {
                system_go_delete_list();
            }
            else if (!hold && ('0' == key))
            {
                system_enter(SYSTEM_STATE_ADMIN_MENU);
            }
            break;

        case SYSTEM_STATE_ADMIN_INPUT:
            if (hold && ('*' == key))
            {
                memset(s_sys.pending_pin, 0, sizeof(s_sys.pending_pin));
                system_enter(SYSTEM_STATE_CRED_MENU);
            }
            else if (!hold && ('#' == key))
            {
                if (0U != s_sys.entry_len)
                {
                    system_admin_submit();
                }
            }
            else if (!hold)
            {
                system_entry_edit(key);
            }
            break;

        case SYSTEM_STATE_ADMIN_SCAN:
            if (hold && ('*' == key))
            {
                system_enter(SYSTEM_STATE_CRED_MENU);
            }
            break;

        case SYSTEM_STATE_ADMIN_LIST:
            system_list_key(key);
            break;

        case SYSTEM_STATE_ADMIN_CONFIRM:
            if (!hold && ('#' == key))
            {
                system_delete_pending(p_fx);
            }
            else if ('*' == key)
            {
                system_go_delete_list(); /* No: back to the list. */
            }
            break;

        default: /* Lockouts: keys do nothing. */
            break;
    }
}

/* The holds of docs/ui_data.md, for the current state. */
static bool system_has_hold(char key)
{
    switch (s_sys.state)
    {
        case SYSTEM_STATE_HOME:
            return ('*' == key) || ('#' == key) || ('0' == key);
        case SYSTEM_STATE_PIN_ENTRY:
            return ('*' == key) || ('0' == key);
        case SYSTEM_STATE_VOICE:
        case SYSTEM_STATE_VOICE_REVIEW:
        case SYSTEM_STATE_ADMIN_LOGIN:
        case SYSTEM_STATE_ADMIN_INPUT:
        case SYSTEM_STATE_ADMIN_SCAN:
        case SYSTEM_STATE_ADMIN_LIST:
        case SYSTEM_STATE_ADMIN_CONFIRM:
        case SYSTEM_STATE_MSG_DELETE:
            return '*' == key;
        default:
            return false;
    }
}

/* AdminLogin: digits and * edit the field that has the keys, # moves from the ID to the password, * on an empty
 * password goes back to the ID. */
static void system_admin_login_key(char key)
{
    if (!s_sys.login_on_password)
    {
        if ('#' == key)
        {
            if (0U != s_sys.entry_len)
            {
                memcpy(s_sys.login_id, s_sys.entry, sizeof(s_sys.login_id));
                s_sys.login_on_password = true;
                system_entry_clear();
            }
            return;
        }
        system_entry_edit(key);
        memcpy(s_sys.login_id, s_sys.entry, sizeof(s_sys.login_id));
        return;
    }

    if (('*' == key) && (0U == s_sys.entry_len))
    {
        s_sys.login_on_password = false;
        memcpy(s_sys.entry, s_sys.login_id, sizeof(s_sys.entry));
        s_sys.entry_len = (uint8_t)strlen(s_sys.entry);
        return;
    }
    system_entry_edit(key);
}

/* Checked here, locally; nothing about a PIN leaves the device except granted / denied. An 8-digit entry that is no PIN
 * may be a temporary guest code made on the web page: it opens as SYSTEM_USER_TEMP (docs/temp_code.md). */
static void system_check_pin(system_effects_t *const p_fx)
{
    uint8_t            user = system_find_pin(s_sys.entry);
    system_temp_code_t temp = SYSTEM_TEMP_CODE_WRONG;

    if ((0U == user) && (SYSTEM_TEMP_CODE_LEN == s_sys.entry_len) && (NULL != s_sys.hooks.check_temp_code))
    {
        temp = s_sys.hooks.check_temp_code(s_sys.entry);
        if (SYSTEM_TEMP_CODE_OK == temp)
        {
            user = SYSTEM_USER_TEMP;
        }
    }
    system_entry_clear();
    if (0U != user)
    {
        ESP_LOGI(SYSTEM_TAG, "%s accepted, user %u", (SYSTEM_USER_TEMP == user) ? "Temporary code" : "PIN", user);
        system_grant(SYSTEM_METHOD_PIN, user, p_fx);
        return;
    }

    /* Counted as wrong even without a clock: otherwise 8-digit guesses of real PINs would be free while it is unset. */
    system_deny(SYSTEM_METHOD_PIN, p_fx);
    if ((SYSTEM_TEMP_CODE_NO_TIME == temp) && (SYSTEM_NOTICE_PIN_WRONG == s_sys.notice))
    {
        ESP_LOGW(SYSTEM_TAG, "Temporary code not checked: the clock is not set");
        system_notify(SYSTEM_NOTICE_TEMP_NO_TIME, s_sys.notice_value);
    }
}

/* A wrong PIN, card or finger. Each method counts its own: too many locks only that method. */
static void system_deny(system_method_t method, system_effects_t *const p_fx)
{
    static const system_notice_t s_notices[]
        = {SYSTEM_NOTICE_PIN_WRONG, SYSTEM_NOTICE_CARD_UNKNOWN, SYSTEM_NOTICE_FINGER_UNKNOWN};

    p_fx->access        = true;
    p_fx->access_method = method;
    if (!system_fail((system_lock_target_t)method, p_fx))
    {
        system_notify(s_notices[method], (uint8_t)(SYSTEM_MAX_TRIES - s_sys.locks[method].fails));
    }
}

static void system_check_admin(system_effects_t *const p_fx)
{
    /* & rather than &&: both are compared, so the time taken does not tell which one was wrong. */
    const bool ok
        = system_text_equal(s_sys.login_id, SYSTEM_ADMIN_ID) & system_text_equal(s_sys.entry, SYSTEM_ADMIN_PASSWORD);

    if (ok)
    {
        /* The admin ends every lockout, also those for good: the only way back for a method locked that way. */
        const uint8_t was_forever = system_forever_mask();
        ESP_LOGI(SYSTEM_TAG, "Admin login%s", (0U != was_forever) ? ", lockouts for good ended" : "");
        memset(s_sys.locks, 0, sizeof(s_sys.locks));
        if (0U != was_forever)
        {
            p_fx->locks_forever = true;
            p_fx->locks_mask    = 0U;
        }
        system_enter(SYSTEM_STATE_ADMIN_MENU);
        return;
    }

    if (!system_fail(SYSTEM_LOCK_ADMIN, p_fx))
    {
        system_enter(SYSTEM_STATE_ADMIN_LOGIN); /* Both fields empty, back to the ID. */
        system_notify(SYSTEM_NOTICE_ADMIN_WRONG,
                      (uint8_t)(SYSTEM_ADMIN_MAX_TRIES - s_sys.locks[SYSTEM_LOCK_ADMIN].fails));
    }
}

/* # on an admin Input step: the new PIN, then the same PIN again. */
static void system_admin_submit(void)
{
    if (SYSTEM_STEP_ADD_PIN == s_sys.step)
    {
        if (s_sys.entry_len < SYSTEM_PIN_MIN_LEN)
        {
            system_notify(SYSTEM_NOTICE_PIN_TOO_SHORT, 0U);
        }
        else if (0U != system_find_pin(s_sys.entry))
        {
            /* Two users with one PIN could not be told apart in the log. */
            system_notify(SYSTEM_NOTICE_PIN_USED, 0U);
        }
        else
        {
            memcpy(s_sys.pending_pin, s_sys.entry, sizeof(s_sys.pending_pin));
            s_sys.step = SYSTEM_STEP_ADD_CONFIRM;
            system_enter(SYSTEM_STATE_ADMIN_INPUT);
        }
        system_entry_clear();
        return;
    }

    /* SYSTEM_STEP_ADD_CONFIRM */
    const bool same = system_text_equal(s_sys.entry, s_sys.pending_pin);
    if (same)
    {
        memcpy(s_pins[s_sys.pending_user - 1U], s_sys.pending_pin, sizeof(s_pins[0]));
        s_sys.cred_seq++;
    }
    memset(s_sys.pending_pin, 0, sizeof(s_sys.pending_pin));
    system_entry_clear();

    if (same)
    {
        ESP_LOGI(SYSTEM_TAG, "PIN added for user %u", s_sys.pending_user);
        system_enter(SYSTEM_STATE_CRED_MENU);
        system_notify(SYSTEM_NOTICE_SAVED, s_sys.pending_user);
    }
    else
    {
        s_sys.step = SYSTEM_STEP_ADD_PIN;
        system_enter(SYSTEM_STATE_ADMIN_INPUT);
        system_notify(SYSTEM_NOTICE_PIN_MISMATCH, 0U);
    }
}

/* 1 on a credential menu: a PIN asks for the user ID, a card or finger is scanned into the first free ID. */
static void system_go_cred_add(void)
{
    if ((SYSTEM_CRED_PIN != s_sys.kind) && (SYSTEM_SLOT_NONE == system_free_slot(s_sys.kind)))
    {
        system_notify(SYSTEM_NOTICE_FULL, 0U);
        return;
    }
    system_list_build(SYSTEM_LIST_PICK_USER);
    if (0U == s_sys.list_count)
    {
        system_notify(SYSTEM_NOTICE_FULL, 0U); /* PIN: every user has one. */
        return;
    }
    system_enter(SYSTEM_STATE_ADMIN_LIST);
}

/* 2 on a credential menu, or No on the confirm screen: everything of the kind, with its owner. */
static void system_go_delete_list(void)
{
    system_list_build(SYSTEM_LIST_DELETE);
    if (0U == s_sys.list_count)
    {
        system_enter(SYSTEM_STATE_CRED_MENU);
        system_notify(SYSTEM_NOTICE_EMPTY, 0U);
        return;
    }
    system_enter(SYSTEM_STATE_ADMIN_LIST);
}

/* Rows of ADMIN_LIST, ordered by user. The highlight starts on the first row. */
static void system_list_build(system_list_purpose_t purpose)
{
    uint8_t n = 0U;

    memset(s_sys.list, 0, sizeof(s_sys.list));
    s_sys.list_purpose = purpose;
    s_sys.list_sel     = 0U;

    if (SYSTEM_LIST_PICK_FINGER == purpose)
    {
        for (uint8_t finger = 0U; finger < SYSTEM_FINGER_COUNT; finger++)
        {
            if (!system_has_finger(s_sys.pending_user, finger))
            {
                s_sys.list[n].user   = s_sys.pending_user;
                s_sys.list[n].finger = finger;
                n++;
            }
        }
        s_sys.list_count = n;
        return;
    }

    for (uint8_t user = 1U; (user <= SYSTEM_USER_COUNT) && (n < SYSTEM_LIST_MAX); user++)
    {
        if (SYSTEM_LIST_PICK_USER == purpose)
        {
            const uint8_t count = system_count_of(s_sys.kind, user);
            if ((SYSTEM_CRED_PIN == s_sys.kind) && (0U != count))
            {
                continue; /* One PIN per user: delete it first to change it. */
            }
            s_sys.list[n].user  = user;
            s_sys.list[n].count = count;
            n++;
            continue;
        }

        /* SYSTEM_LIST_DELETE */
        switch (s_sys.kind)
        {
            case SYSTEM_CRED_PIN:
                if ('\0' != s_pins[user - 1U][0])
                {
                    s_sys.list[n++].user = user;
                }
                break;
            case SYSTEM_CRED_CARD:
                for (uint8_t slot = 0U; (slot < SYSTEM_CARD_SLOTS) && (n < SYSTEM_LIST_MAX); slot++)
                {
                    if (user == s_card_user[slot])
                    {
                        const uint8_t len      = s_card_len[slot];
                        s_sys.list[n].user     = user;
                        s_sys.list[n].slot     = slot;
                        s_sys.list[n].uid_tail = (uint16_t)((s_cards[slot][len - 2U] << 8) | s_cards[slot][len - 1U]);
                        n++;
                    }
                }
                break;
            default:
                for (uint8_t finger = 0U; finger <= SYSTEM_FINGER_UNKNOWN; finger++)
                {
                    for (uint8_t slot = 0U; (slot < SYSTEM_FINGER_SLOTS) && (n < SYSTEM_LIST_MAX); slot++)
                    {
                        if ((user == s_finger_user[slot]) && (finger == s_finger_id[slot]))
                        {
                            s_sys.list[n].user   = user;
                            s_sys.list[n].finger = finger;
                            s_sys.list[n].slot   = slot;
                            n++;
                        }
                    }
                }
                break;
        }
    }
    s_sys.list_count = n;
}

/* ADMIN_LIST: 2 / 8 move the highlight (and wrap), # picks the row, * goes back. */
static void system_list_key(char key)
{
    const system_list_item_t *const p_item = &s_sys.list[s_sys.list_sel];

    switch (key)
    {
        case '2':
            s_sys.list_sel = (0U == s_sys.list_sel) ? (uint8_t)(s_sys.list_count - 1U) : (uint8_t)(s_sys.list_sel - 1U);
            break;
        case '8':
            s_sys.list_sel = ((s_sys.list_sel + 1U) >= s_sys.list_count) ? 0U : (uint8_t)(s_sys.list_sel + 1U);
            break;
        case '*':
            if (SYSTEM_LIST_PICK_FINGER == s_sys.list_purpose)
            {
                system_list_build(SYSTEM_LIST_PICK_USER); /* Back to whose finger. */
                system_enter(SYSTEM_STATE_ADMIN_LIST);
            }
            else
            {
                system_enter(SYSTEM_STATE_CRED_MENU);
            }
            break;
        case '#':
            s_sys.pending_user   = p_item->user;
            s_sys.pending_finger = p_item->finger;
            s_sys.pending_slot   = p_item->slot;
            if (SYSTEM_LIST_DELETE == s_sys.list_purpose)
            {
                system_enter(SYSTEM_STATE_ADMIN_CONFIRM);
            }
            else if (SYSTEM_CRED_PIN == s_sys.kind)
            {
                s_sys.step = SYSTEM_STEP_ADD_PIN;
                system_enter(SYSTEM_STATE_ADMIN_INPUT);
            }
            else if ((SYSTEM_CRED_FINGER == s_sys.kind) && (SYSTEM_LIST_PICK_USER == s_sys.list_purpose))
            {
                system_list_build(SYSTEM_LIST_PICK_FINGER);
                if (0U == s_sys.list_count)
                {
                    system_list_build(SYSTEM_LIST_PICK_USER);
                    system_notify(SYSTEM_NOTICE_ALL_FINGERS, s_sys.pending_user);
                    break;
                }
                system_enter(SYSTEM_STATE_ADMIN_LIST);
            }
            else
            {
                system_scan_start(); /* A card for that user, or the finger just picked. */
            }
            break;
        default:
            break;
    }
}

static void system_scan_start(void)
{
    const uint8_t slot = system_free_slot(s_sys.kind);

    if (SYSTEM_SLOT_NONE == slot)
    {
        system_enter(SYSTEM_STATE_CRED_MENU);
        system_notify(SYSTEM_NOTICE_FULL, 0U);
        return;
    }
    system_enter(SYSTEM_STATE_ADMIN_SCAN);
    s_sys.scan_slot    = slot;
    s_sys.scan_count   = 0U;
    s_sys.scan_uid_len = 0U;
}

/* Hold * on Home (a message for the owner) or 1 on MESSAGES FOR GUESTS (owner: a message for guests). */
static void system_voice_start(bool owner, system_effects_t *const p_fx)
{
    if (!s_sys.voice_ready)
    {
        system_notify(SYSTEM_NOTICE_MSG_UNAVAILABLE, 0U);
        return;
    }
    ESP_LOGI(SYSTEM_TAG, "Voice recording started (%s)", owner ? "for guests" : "for the owner");
    s_sys.msg_admin = owner;
    system_enter(SYSTEM_STATE_VOICE);
    p_fx->voice        = true;
    p_fx->voice_action = SYSTEM_VOICE_START;
    p_fx->voice_owner  = owner;
}

/* Out of the recording screen: the admin back to the messages menu, a guest to Home. */
static void system_voice_leave(void)
{
    if (s_sys.msg_admin)
    {
        system_enter(SYSTEM_STATE_MSG_MENU);
    }
    else
    {
        system_go_home();
    }
}

/* Recording stopped (# or 15 s): to the review. The voice code then gives the length (voice_recorded), or a failure
 * (voice_result) that leaves the review. */
static void system_voice_review(system_effects_t *const p_fx, system_voice_t action)
{
    p_fx->voice          = true;
    p_fx->voice_action   = action;
    p_fx->voice_owner    = s_sys.msg_admin;
    s_sys.review_seconds = 0U;
    system_enter(SYSTEM_STATE_VOICE_REVIEW);
}

/* VOICE_REVIEW: 1 listens, 2 records again, # keeps it (a guest's goes to the owner), hold * drops it. */
static void system_review_key(char key, bool hold, system_effects_t *const p_fx)
{
    if (hold && ('*' == key))
    {
        ESP_LOGI(SYSTEM_TAG, "Recording dropped after the review");
        p_fx->voice        = true;
        p_fx->voice_action = SYSTEM_VOICE_CANCEL;
        p_fx->voice_owner  = s_sys.msg_admin;
        system_voice_leave();
        system_notify(SYSTEM_NOTICE_VOICE_CANCELLED, 0U);
    }
    else if (('1' == key) && (0U != s_sys.review_seconds))
    {
        s_sys.play_from    = SYSTEM_STATE_VOICE_REVIEW;
        s_sys.play_seconds = s_sys.review_seconds;
        system_enter(SYSTEM_STATE_PLAYING);
        s_sys.play_token++;
        p_fx->play       = true;
        p_fx->play_slot  = SYSTEM_PLAY_RECORDING;
        p_fx->play_token = s_sys.play_token;
    }
    else if ('2' == key)
    {
        system_voice_start(s_sys.msg_admin, p_fx); /* The new recording replaces this one. */
    }
    else if (('#' == key) && (0U != s_sys.review_seconds))
    {
        ESP_LOGI(SYSTEM_TAG, "Recording kept after the review");
        p_fx->voice        = true;
        p_fx->voice_action = SYSTEM_VOICE_SEND;
        p_fx->voice_owner  = s_sys.msg_admin;
        system_voice_leave(); /* The voice code says when it is kept (MSG_SENT / MSG_SAVED). */
    }
}

/* The owner's messages as a list. Nothing to list: back where the list was opened from. */
static void system_msg_list_open(bool admin)
{
    s_sys.msg_admin = admin;
    if (0U == s_sys.message_count)
    {
        if (admin)
        {
            system_enter(SYSTEM_STATE_MSG_MENU);
            system_notify(SYSTEM_NOTICE_EMPTY, 0U);
        }
        else
        {
            system_go_home();
        }
        return;
    }
    s_sys.list_count = s_sys.message_count;
    s_sys.list_sel   = 0U;
    system_enter(SYSTEM_STATE_MSG_LIST);
}

/* MSG_LIST: 2 / 8 move, # plays, * goes back; the admin also deletes with 0. */
static void system_msg_list_key(char key, system_effects_t *const p_fx)
{
    switch (key)
    {
        case '2':
            s_sys.list_sel = (0U == s_sys.list_sel) ? (uint8_t)(s_sys.list_count - 1U) : (uint8_t)(s_sys.list_sel - 1U);
            break;
        case '8':
            s_sys.list_sel = ((s_sys.list_sel + 1U) >= s_sys.list_count) ? 0U : (uint8_t)(s_sys.list_sel + 1U);
            break;
        case '#':
            system_play(s_sys.list_sel, SYSTEM_STATE_MSG_LIST, p_fx);
            break;
        case '0':
            if (s_sys.msg_admin)
            {
                const uint8_t sel  = s_sys.list_sel;
                s_sys.pending_slot = s_sys.messages[sel].slot;
                system_enter(SYSTEM_STATE_MSG_DELETE);
                s_sys.list_sel = sel;
            }
            break;
        case '*':
            if (s_sys.msg_admin)
            {
                system_enter(SYSTEM_STATE_MSG_MENU);
            }
            else
            {
                system_go_home();
            }
            break;
        default:
            break;
    }
}

/* Plays the index-th message of the list. */
static void system_play(uint8_t index, system_state_t from, system_effects_t *const p_fx)
{
    const uint8_t sel = s_sys.list_sel;

    if (index >= s_sys.message_count)
    {
        return;
    }
    ESP_LOGI(SYSTEM_TAG, "Playing message %u (slot %u)", index + 1U, s_sys.messages[index].slot);
    s_sys.play_from    = from;
    s_sys.play_seconds = s_sys.messages[index].seconds;
    system_enter(SYSTEM_STATE_PLAYING);
    s_sys.list_sel = sel; /* Back on the same row. */
    s_sys.play_token++;
    p_fx->play       = true;
    p_fx->play_slot  = s_sys.messages[index].slot;
    p_fx->play_token = s_sys.play_token;
}

static void system_play_leave(void)
{
    const uint8_t sel = s_sys.list_sel;

    if (SYSTEM_STATE_VOICE_REVIEW == s_sys.play_from)
    {
        system_enter(SYSTEM_STATE_VOICE_REVIEW); /* review_seconds is kept. */
    }
    else if ((SYSTEM_STATE_MSG_LIST == s_sys.play_from) && (0U != s_sys.message_count))
    {
        s_sys.list_count = s_sys.message_count;
        system_enter(SYSTEM_STATE_MSG_LIST);
        s_sys.list_sel = (sel < s_sys.list_count) ? sel : 0U;
    }
    else
    {
        system_go_home();
    }
}

/* Row of the message in this slot, or SYSTEM_SLOT_NONE. */
static uint8_t system_msg_index(uint8_t slot)
{
    for (uint8_t i = 0U; i < s_sys.message_count; i++)
    {
        if (slot == s_sys.messages[i].slot)
        {
            return i;
        }
    }
    return SYSTEM_SLOT_NONE;
}

/* Yes on the confirm screen. */
static void system_delete_pending(system_effects_t *const p_fx)
{
    const uint8_t user   = s_sys.pending_user;
    const uint8_t finger = s_sys.pending_finger;
    const uint8_t slot   = s_sys.pending_slot;

    switch (s_sys.kind)
    {
        case SYSTEM_CRED_PIN:
            memset(s_pins[user - 1U], 0, sizeof(s_pins[0]));
            break;
        case SYSTEM_CRED_CARD:
            s_card_user[slot] = 0U;
            memset(s_cards[slot], 0, sizeof(s_cards[0]));
            s_card_len[slot] = 0U;
            break;
        default:
            s_finger_user[slot] = 0U;
            s_finger_id[slot]   = 0U;
            p_fx->finger_delete = (uint8_t)(slot + 1U); /* Its page in the sensor. */
            break;
    }
    s_sys.cred_seq++;
    ESP_LOGI(SYSTEM_TAG, "Credential %d of user %u deleted (slot %u)", (int)s_sys.kind, user, slot);
    system_enter(SYSTEM_STATE_CRED_MENU);
    system_notify2(SYSTEM_NOTICE_DELETED, user, finger);
}

/* One more good scan. The SYSTEM_SCAN_COUNT-th returns true: the caller saves, and the admin is back in the menu.
 * Before that the scan timeout starts again, so each scan gets the full time. */
static bool system_scan_good(void)
{
    s_sys.scan_count++;
    if (s_sys.scan_count < SYSTEM_SCAN_COUNT)
    {
        s_sys.state_tick = xTaskGetTickCount();
        return false;
    }
    system_enter(SYSTEM_STATE_CRED_MENU);
    system_notify2(SYSTEM_NOTICE_SAVED, s_sys.pending_user, s_sys.pending_finger);
    return true;
}

static void system_grant(system_method_t method, uint8_t user, system_effects_t *const p_fx)
{
    /* A good unlock with any method clears the tries and the timed lockouts of all three methods; a lockout for good
     * stays until an admin login, and the admin login keeps its own count. */
    for (uint8_t i = 0U; i < (uint8_t)SYSTEM_LOCK_ADMIN; i++)
    {
        s_sys.locks[i].fails    = 0U;
        s_sys.locks[i].lockouts = 0U;
        s_sys.locks[i].lock_ms  = 0U;
    }
    s_sys.door_unlocked  = true;
    s_sys.unlock_tick    = xTaskGetTickCount();
    p_fx->access         = true;
    p_fx->access_method  = method;
    p_fx->access_granted = true;
    p_fx->access_user    = user;
    p_fx->door           = true;
    p_fx->door_unlocked  = true;
    system_go_home();
    system_notify(SYSTEM_NOTICE_UNLOCKED, user);
}

/* One wrong try of target. True when it was the last one allowed: target is now locked and the screen says so. */
static bool system_fail(system_lock_target_t target, system_effects_t *const p_fx)
{
    static const char *const s_names[SYSTEM_LOCK_COUNT] = {"PIN", "card", "finger", "admin login"};
    system_lock_t *const     p_lock                     = &s_sys.locks[target];
    const uint8_t max = (SYSTEM_LOCK_ADMIN == target) ? SYSTEM_ADMIN_MAX_TRIES : SYSTEM_MAX_TRIES;

    if (p_lock->fails < UINT8_MAX)
    {
        p_lock->fails++;
    }
    ESP_LOGW(SYSTEM_TAG, "Wrong %s (%u / %u)", s_names[target], p_lock->fails, max);
    if (p_lock->fails < max)
    {
        return false;
    }
    system_lockout(target, p_fx);
    return true;
}

/* Locks target alone: 30 s, 1 min, 5 min, 15 min for lockouts in a row, then for good (SYSTEM_LOCKOUT_STEPS_MS). The
 * admin login always for SYSTEM_ADMIN_LOCKOUT_MS. */
static void system_lockout(system_lock_target_t target, system_effects_t *const p_fx)
{
    static const uint32_t s_steps_ms[] = SYSTEM_LOCKOUT_STEPS_MS;
    const uint8_t         steps        = (uint8_t)(sizeof(s_steps_ms) / sizeof(s_steps_ms[0]));
    system_lock_t *const  p_lock       = &s_sys.locks[target];

    p_lock->fails     = 0U; /* A fresh set of tries once it ends. */
    p_lock->lock_tick = xTaskGetTickCount();
    if (SYSTEM_LOCK_ADMIN == target)
    {
        p_lock->lock_ms = SYSTEM_ADMIN_LOCKOUT_MS;
    }
    else if (p_lock->lockouts < steps)
    {
        p_lock->lock_ms = s_steps_ms[p_lock->lockouts];
        p_lock->lockouts++;
    }
    else
    {
        p_lock->lock_ms     = 0U;
        p_lock->forever     = true;
        p_fx->locks_forever = true;
        p_fx->locks_mask    = system_forever_mask();
    }

    const uint32_t left_s = system_lock_left_s(target);
    if (SYSTEM_LOCK_FOREVER == left_s)
    {
        ESP_LOGW(SYSTEM_TAG, "Lockout %u: locked until an admin login", (unsigned)target);
    }
    else
    {
        ESP_LOGW(SYSTEM_TAG,
                 "Lockout %u: %u of %u, %lu s",
                 (unsigned)target,
                 (unsigned)p_lock->lockouts,
                 (unsigned)steps,
                 (unsigned long)left_s);
    }
    p_fx->alert        = true;
    p_fx->alert_code   = SYSTEM_ALERT_LOCKOUT;
    p_fx->alert_target = target;
    p_fx->alert_lock_s = left_s;

    if ((SYSTEM_LOCK_PIN == target) || (SYSTEM_LOCK_ADMIN == target))
    {
        system_go_home(); /* Leave the field that can no longer be used. */
    }
    s_sys.notice_lock_s = left_s;
    system_notify2(SYSTEM_NOTICE_LOCKED, (uint8_t)target, 1U);
}

/* True while target is locked; the popup then says for how long. */
static bool system_locked(system_lock_target_t target)
{
    const uint32_t left_s = system_lock_left_s(target);

    if (0U == left_s)
    {
        return false;
    }
    s_sys.notice_lock_s = left_s;
    system_notify2(SYSTEM_NOTICE_LOCKED, (uint8_t)target, 0U);
    return true;
}

/* Seconds left of the lockout of target: 0 when it is not locked, SYSTEM_LOCK_FOREVER until an admin login. */
static uint32_t system_lock_left_s(system_lock_target_t target)
{
    const system_lock_t *const p_lock = &s_sys.locks[target];

    if (p_lock->forever)
    {
        return SYSTEM_LOCK_FOREVER;
    }
    if (0U == p_lock->lock_ms)
    {
        return 0U;
    }
    const uint32_t elapsed_ms = system_elapsed_ms(p_lock->lock_tick, xTaskGetTickCount());
    return (elapsed_ms >= p_lock->lock_ms) ? 0U : (((p_lock->lock_ms - elapsed_ms) + 999U) / 1000U);
}

/* Bit n: target n locked for good. */
static uint8_t system_forever_mask(void)
{
    uint8_t mask = 0U;

    for (uint8_t i = 0U; i < (uint8_t)SYSTEM_LOCK_COUNT; i++)
    {
        if (s_sys.locks[i].forever)
        {
            mask |= (uint8_t)(1U << i);
        }
    }
    return mask;
}

/***********************************************************************************************************************
 * Helpers
 **********************************************************************************************************************/

static bool system_lock(void)
{
    if (!s_sys.init)
    {
        return false;
    }
    if (pdTRUE != xSemaphoreTake(s_sys.mutex, pdMS_TO_TICKS(SYSTEM_LOCK_WAIT_MS)))
    {
        ESP_LOGW(SYSTEM_TAG, "Lock timeout, input dropped");
        return false;
    }
    return true;
}

static void system_unlock_and_apply(const system_effects_t *const p_fx)
{
    const system_control_hooks_t hooks = s_sys.hooks;
    xSemaphoreGive(s_sys.mutex);

    if (p_fx->door && (NULL != hooks.on_door))
    {
        hooks.on_door(p_fx->door_unlocked);
    }
    if (p_fx->access && (NULL != hooks.on_access))
    {
        hooks.on_access(p_fx->access_method, p_fx->access_granted, p_fx->access_user);
    }
    if (p_fx->alert && (NULL != hooks.on_alert))
    {
        hooks.on_alert(p_fx->alert_code, p_fx->alert_target, p_fx->alert_lock_s);
    }
    if (p_fx->locks_forever && (NULL != hooks.on_locks_forever))
    {
        hooks.on_locks_forever(p_fx->locks_mask);
    }
    if (p_fx->voice && (NULL != hooks.on_voice))
    {
        hooks.on_voice(p_fx->voice_action, p_fx->voice_owner);
    }
    if (p_fx->play_stop && (NULL != hooks.on_play_stop))
    {
        hooks.on_play_stop();
    }
    if (p_fx->play && (NULL != hooks.on_play))
    {
        hooks.on_play(p_fx->play_slot, p_fx->play_token);
    }
    if ((0U != p_fx->message_delete) && (NULL != hooks.on_message_delete))
    {
        hooks.on_message_delete((uint8_t)(p_fx->message_delete - 1U));
    }
    if ((0U != p_fx->finger_delete) && (NULL != hooks.on_finger_delete))
    {
        hooks.on_finger_delete(p_fx->finger_delete);
    }
}

/* Every screen change starts with empty fields and a fresh idle timer. */
static void system_enter(system_state_t state)
{
    s_sys.state             = state;
    s_sys.state_tick        = xTaskGetTickCount();
    s_sys.activity_tick     = s_sys.state_tick;
    s_sys.login_on_password = false;
    memset(s_sys.login_id, 0, sizeof(s_sys.login_id));
    system_entry_clear();
}

static void system_notify(system_notice_t notice, uint8_t value)
{
    system_notify2(notice, value, 0U);
}

static void system_notify2(system_notice_t notice, uint8_t value, uint8_t value2)
{
    s_sys.notice        = notice;
    s_sys.notice_value  = value;
    s_sys.notice_value2 = value2;
    s_sys.notice_seq++;
}

static void system_entry_clear(void)
{
    memset(s_sys.entry, 0, sizeof(s_sys.entry));
    s_sys.entry_len = 0U;
}

/* Digits append, * deletes the last one, # is handled by the caller. */
static void system_entry_edit(char key)
{
    if (('*' == key) && (0U != s_sys.entry_len))
    {
        s_sys.entry_len--;
        s_sys.entry[s_sys.entry_len] = '\0';
    }
    else if ((key >= '0') && (key <= '9') && (s_sys.entry_len < SYSTEM_ENTRY_MAX))
    {
        s_sys.entry[s_sys.entry_len] = key;
        s_sys.entry_len++;
    }
}

static void system_go_home(void)
{
    memset(s_sys.pending_pin, 0, sizeof(s_sys.pending_pin));
    s_sys.msg_admin = false;
    system_enter(SYSTEM_STATE_HOME);
}

/* Wraps correctly with the tick counter. */
static uint32_t system_elapsed_ms(TickType_t since, TickType_t now)
{
    return (uint32_t)(now - since) * portTICK_PERIOD_MS;
}

/* Compares every character whatever the first difference, so timing does not leak how much of a code was right. */
static bool system_text_equal(const char *p_a, const char *p_b)
{
    const size_t len_a = strlen(p_a);
    const size_t len_b = strlen(p_b);
    uint8_t      diff  = (len_a != len_b) ? 1U : 0U;

    for (size_t i = 0U; i < SYSTEM_ENTRY_MAX; i++)
    {
        const char char_a = (i < len_a) ? p_a[i] : '\0';
        const char char_b = (i < len_b) ? p_b[i] : '\0';
        diff |= (uint8_t)(char_a ^ char_b);
    }
    return 0U == diff;
}

/* SYSTEM_PIN_MIN_LEN..SYSTEM_ENTRY_MAX digits, as the admin menu accepts. */
static bool system_pin_valid(const char *p_pin)
{
    const size_t len = strnlen(p_pin, SYSTEM_ENTRY_MAX + 1U);

    if ((len < SYSTEM_PIN_MIN_LEN) || (len > SYSTEM_ENTRY_MAX))
    {
        return false;
    }
    for (size_t i = 0U; i < len; i++)
    {
        if ((p_pin[i] < '0') || (p_pin[i] > '9'))
        {
            return false;
        }
    }
    return true;
}

/* User ID owning this PIN, or 0. */
static uint8_t system_find_pin(const char *p_pin)
{
    uint8_t found = 0U;

    for (uint8_t i = 0U; i < SYSTEM_USER_COUNT; i++)
    {
        if (('\0' != s_pins[i][0]) && system_text_equal(p_pin, s_pins[i]) && (0U == found))
        {
            found = (uint8_t)(i + 1U);
        }
    }
    return found;
}

static uint8_t system_find_card(const uint8_t *p_uid, uint8_t uid_length)
{
    for (uint8_t i = 0U; i < SYSTEM_CARD_SLOTS; i++)
    {
        if ((0U != s_card_user[i]) && (s_card_len[i] == uid_length) && (0 == memcmp(s_cards[i], p_uid, uid_length)))
        {
            return s_card_user[i];
        }
    }
    return 0U;
}

/* First free card / finger slot, or SYSTEM_SLOT_NONE. */
static uint8_t system_free_slot(system_cred_kind_t kind)
{
    const uint8_t *const p_users = (SYSTEM_CRED_CARD == kind) ? s_card_user : s_finger_user;
    const uint8_t        slots   = (SYSTEM_CRED_CARD == kind) ? SYSTEM_CARD_SLOTS : SYSTEM_FINGER_SLOTS;

    for (uint8_t slot = 0U; slot < slots; slot++)
    {
        if (0U == p_users[slot])
        {
            return slot;
        }
    }
    return SYSTEM_SLOT_NONE;
}

/* PINs (0 or 1), cards or fingers of one user. */
static uint8_t system_count_of(system_cred_kind_t kind, uint8_t user)
{
    uint8_t count = 0U;

    switch (kind)
    {
        case SYSTEM_CRED_PIN:
            return ('\0' != s_pins[user - 1U][0]) ? 1U : 0U;
        case SYSTEM_CRED_CARD:
            for (uint8_t slot = 0U; slot < SYSTEM_CARD_SLOTS; slot++)
            {
                count += (user == s_card_user[slot]) ? 1U : 0U;
            }
            return count;
        default:
            for (uint8_t slot = 0U; slot < SYSTEM_FINGER_SLOTS; slot++)
            {
                count += (user == s_finger_user[slot]) ? 1U : 0U;
            }
            return count;
    }
}

/* Entries of the kind, all users together. */
static uint8_t system_user_count(system_cred_kind_t kind)
{
    uint8_t count = 0U;

    for (uint8_t user = 1U; user <= SYSTEM_USER_COUNT; user++)
    {
        count = (uint8_t)(count + system_count_of(kind, user));
    }
    return count;
}

static bool system_has_finger(uint8_t user, uint8_t finger)
{
    for (uint8_t slot = 0U; slot < SYSTEM_FINGER_SLOTS; slot++)
    {
        if ((user == s_finger_user[slot]) && (finger == s_finger_id[slot]))
        {
            return true;
        }
    }
    return false;
}
