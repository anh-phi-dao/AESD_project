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

#define SYSTEM_TAG          "SYSTEM_CONTROL"
#define SYSTEM_LOCK_WAIT_MS 100U

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/* Hook calls decided under the lock and made after it, so a slow hook never holds up the keypad or the screen. */
typedef struct st_system_effects
{
    bool            access;
    system_method_t access_method;
    bool            access_granted;
    uint8_t         access_user;
    bool            alert;
    system_alert_t  alert_code;
    bool            door;
    bool            door_unlocked;
    bool            voice;
    system_voice_t  voice_action;
    bool            play;
} system_effects_t;

typedef struct st_system_ctx
{
    SemaphoreHandle_t      mutex;
    system_control_hooks_t hooks;
    system_state_t         state;
    system_cred_kind_t     kind;
    system_admin_step_t    step;
    bool                   login_on_password;
    char                   entry[SYSTEM_ENTRY_MAX + 1U]; /* The field that has the keys. */
    uint8_t                entry_len;
    char                   login_id[SYSTEM_ENTRY_MAX + 1U];
    uint8_t                pending_user;                       /* Admin add / delete: chosen user ID. */
    char                   pending_pin[SYSTEM_ENTRY_MAX + 1U]; /* Admin add: first entry of the new PIN. */
    TickType_t             state_tick;
    TickType_t             activity_tick;
    TickType_t             unlock_tick;
    TickType_t             card_tick;
    uint8_t                last_uid[SYSTEM_UID_MAX];
    uint8_t                last_uid_len;
    uint8_t                scan_user;                /* ADMIN_SCAN: ID the card / finger goes to. */
    uint8_t                scan_count;               /* ADMIN_SCAN: good scans so far. */
    uint8_t                scan_uid[SYSTEM_UID_MAX]; /* ADMIN_SCAN: card of the first scan. */
    uint8_t                scan_uid_len;
    bool                   door_unlocked;
    uint8_t                fails; /* Wrong PINs, unknown cards and fingers, counted together. */
    uint8_t                admin_fails;
    uint8_t                lockouts; /* In a row, without a good code between them. */
    bool                   lock_forever;
    uint8_t                message_count;
    uint32_t               notice_seq;
    system_notice_t        notice;
    uint8_t                notice_value;
    bool                   init;
} system_ctx_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static bool     system_lock(void);
static void     system_unlock_and_apply(const system_effects_t *const p_fx);
static void     system_enter(system_state_t state);
static void     system_notify(system_notice_t notice, uint8_t value);
static void     system_entry_clear(void);
static void     system_entry_edit(char key);
static void     system_go_home(void);
static void     system_handle_key(char key, bool hold, system_effects_t *const p_fx);
static bool     system_has_hold(char key);
static void     system_admin_login_key(char key);
static void     system_check_pin(system_effects_t *const p_fx);
static void     system_deny(system_method_t method, system_effects_t *const p_fx);
static void     system_check_admin(system_effects_t *const p_fx);
static void     system_admin_submit(void);
static void     system_go_cred_add(void);
static bool     system_scan_good(void);
static void     system_grant(system_method_t method, uint8_t user, system_effects_t *const p_fx);
static void     system_lockout(bool admin, system_effects_t *const p_fx);
static uint32_t system_elapsed_ms(TickType_t since, TickType_t now);
static bool     system_text_equal(const char *p_a, const char *p_b);
static uint8_t  system_find_pin(const char *p_pin);
static uint8_t  system_find_card(const uint8_t *p_uid, uint8_t uid_length);
static uint8_t  system_parse_user(const char *p_text);
static bool     system_user_has(system_cred_kind_t kind, uint8_t user);
static uint8_t  system_first_free(system_cred_kind_t kind);
static uint8_t  system_user_count(system_cred_kind_t kind);
static void     system_user_delete(system_cred_kind_t kind, uint8_t user);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

static system_ctx_t s_sys = {0};

/* Credentials, indexed by user ID - 1. RAM only until storage keeps them in flash (see SYSTEM_DEFAULT_PIN). */
static char    s_pins[SYSTEM_USER_COUNT][SYSTEM_ENTRY_MAX + 1U];
static uint8_t s_cards[SYSTEM_USER_COUNT][SYSTEM_UID_MAX];
static uint8_t s_card_len[SYSTEM_USER_COUNT]; /* 0: no card. */
static bool    s_fingers[SYSTEM_USER_COUNT];

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

        case SYSTEM_STATE_LOCKOUT:
        case SYSTEM_STATE_ADMIN_LOCKOUT:
            /* The lockout stops guessing on the keypad; a registered card still opens, and ends the lockout, so wrong
             * PINs from a stranger cannot shut the owner out until a restart. Unknown cards are ignored here. */
            if (0U != user)
            {
                ESP_LOGI(SYSTEM_TAG, "Card accepted during lockout, user %u", user);
                s_sys.admin_fails = 0U;
                system_grant(SYSTEM_METHOD_CARD, user, &fx);
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
                memcpy(s_cards[s_sys.scan_user - 1U], p_uid, uid_length);
                s_card_len[s_sys.scan_user - 1U] = uid_length;
                ESP_LOGI(SYSTEM_TAG, "Card added for user %u", s_sys.scan_user);
            }
            break;

        default:
            break;
    }
    system_unlock_and_apply(&fx);
}

void system_control_finger(uint8_t user)
{
    system_effects_t fx = {0};

    if ((user > SYSTEM_USER_COUNT) || !system_lock())
    {
        return;
    }
    const bool known = (0U != user) && s_fingers[user - 1U];

    switch (s_sys.state)
    {
        case SYSTEM_STATE_HOME:
        case SYSTEM_STATE_PIN_ENTRY:
            if (known)
            {
                ESP_LOGI(SYSTEM_TAG, "Finger accepted, user %u", user);
                system_grant(SYSTEM_METHOD_FINGER, user, &fx);
            }
            else
            {
                system_deny(SYSTEM_METHOD_FINGER, &fx);
            }
            break;

        case SYSTEM_STATE_LOCKOUT:
        case SYSTEM_STATE_ADMIN_LOCKOUT:
            /* Like a registered card: opens and ends the lockout. */
            if (known)
            {
                s_sys.admin_fails = 0U;
                system_grant(SYSTEM_METHOD_FINGER, user, &fx);
            }
            break;

        default: /* TODO(fingerprint driver): enrolling in ADMIN_SCAN marks s_fingers[] here. */
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
            s_fingers[s_sys.scan_user - 1U] = true;
            ESP_LOGI(SYSTEM_TAG, "Fingerprint added for user %u", s_sys.scan_user);
        }
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
            if (idle_ms >= SYSTEM_ADMIN_IDLE_MS)
            {
                ESP_LOGI(SYSTEM_TAG, "Admin mode timed out");
                system_go_home();
                system_notify(SYSTEM_NOTICE_ADMIN_TIMEOUT, 0U);
            }
            break;

        case SYSTEM_STATE_LOCKOUT:
        case SYSTEM_STATE_ADMIN_LOCKOUT:
            if (!s_sys.lock_forever && (state_ms >= SYSTEM_LOCKOUT_MS))
            {
                /* A fresh set of tries: otherwise the first wrong code after a lockout would lock again. */
                s_sys.fails       = 0U;
                s_sys.admin_fails = 0U;
                system_go_home();
            }
            break;

        case SYSTEM_STATE_VOICE:
            if (state_ms >= SYSTEM_VOICE_MAX_MS)
            {
                fx.voice        = true;
                fx.voice_action = SYSTEM_VOICE_STOP;
                system_go_home();
                system_notify(SYSTEM_NOTICE_VOICE_STOPPED, 0U);
            }
            break;

        case SYSTEM_STATE_ADMIN_SCAN:
            if (state_ms >= SYSTEM_SCAN_TIMEOUT_MS)
            {
                /* TODO(fingerprint driver): enroll in the sensor. Cards come through system_control_card(). */
                system_enter(SYSTEM_STATE_CRED_MENU);
                system_notify(SYSTEM_NOTICE_SCAN_TIMEOUT, 0U);
            }
            break;

        default:
            break;
    }
    system_unlock_and_apply(&fx);
}

bool system_control_keys_enabled(void)
{
    /* One word read: no lock needed for a hint. */
    const system_state_t state = s_sys.state;
    return (SYSTEM_STATE_LOCKOUT != state) && (SYSTEM_STATE_ADMIN_LOCKOUT != state);
}

void system_control_set_message_count(uint8_t count)
{
    if (system_lock())
    {
        s_sys.message_count = count;
        xSemaphoreGive(s_sys.mutex);
    }
}

bool system_control_get_view(system_view_t *const p_view)
{
    if ((NULL == p_view) || !s_sys.init || (pdTRUE != xSemaphoreTake(s_sys.mutex, pdMS_TO_TICKS(SYSTEM_LOCK_WAIT_MS))))
    {
        return false;
    }

    const bool shows_id = ((SYSTEM_STATE_ADMIN_LOGIN == s_sys.state) && !s_sys.login_on_password)
                          || ((SYSTEM_STATE_ADMIN_INPUT == s_sys.state)
                              && ((SYSTEM_STEP_ADD_ID == s_sys.step) || (SYSTEM_STEP_DELETE_ID == s_sys.step)));

    memset(p_view, 0, sizeof(*p_view));
    p_view->state             = s_sys.state;
    p_view->kind              = s_sys.kind;
    p_view->step              = s_sys.step;
    p_view->door_unlocked     = s_sys.door_unlocked;
    p_view->lock_forever      = s_sys.lock_forever;
    p_view->login_on_password = s_sys.login_on_password;
    p_view->entry_len         = s_sys.entry_len;
    if (shows_id)
    {
        memcpy(p_view->entry_text, s_sys.entry, sizeof(p_view->entry_text));
    }
    memcpy(p_view->login_id, s_sys.login_id, sizeof(p_view->login_id));
    p_view->stored_count  = system_user_count(s_sys.kind);
    p_view->message_count = s_sys.message_count;
    p_view->state_ms      = system_elapsed_ms(s_sys.state_tick, xTaskGetTickCount());
    p_view->notice_seq    = s_sys.notice_seq;
    p_view->notice        = s_sys.notice;
    p_view->notice_value  = s_sys.notice_value;
    p_view->scan_user     = s_sys.scan_user;
    p_view->scan_count    = s_sys.scan_count;
    xSemaphoreGive(s_sys.mutex);
    return true;
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
                ESP_LOGI(SYSTEM_TAG, "Voice recording started");
                system_enter(SYSTEM_STATE_VOICE);
                p_fx->voice        = true;
                p_fx->voice_action = SYSTEM_VOICE_START;
            }
            else if (hold && ('#' == key))
            {
                if (0U != s_sys.message_count)
                {
                    p_fx->play = true;
                    system_notify(SYSTEM_NOTICE_PLAYING, 0U);
                }
                else
                {
                    system_notify(SYSTEM_NOTICE_NO_MESSAGES, 0U);
                }
            }
            else if (hold && ('0' == key))
            {
                system_enter(SYSTEM_STATE_ADMIN_LOGIN);
            }
            else if (!hold && digit)
            {
                system_enter(SYSTEM_STATE_PIN_ENTRY);
                system_entry_edit(key);
            }
            break;

        case SYSTEM_STATE_PIN_ENTRY:
            if (hold && ('*' == key))
            {
                system_go_home();
            }
            else if (hold && ('0' == key))
            {
                system_enter(SYSTEM_STATE_ADMIN_LOGIN);
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
                p_fx->voice        = true;
                p_fx->voice_action = SYSTEM_VOICE_STOP;
                system_go_home();
                system_notify(SYSTEM_NOTICE_VOICE_STOPPED, 0U);
            }
            else if (hold && ('*' == key))
            {
                ESP_LOGI(SYSTEM_TAG, "Voice recording cancelled");
                p_fx->voice        = true;
                p_fx->voice_action = SYSTEM_VOICE_CANCEL;
                system_go_home();
                system_notify(SYSTEM_NOTICE_VOICE_CANCELLED, 0U);
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
                s_sys.step = SYSTEM_STEP_DELETE_ID;
                system_enter(SYSTEM_STATE_ADMIN_INPUT);
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
        case SYSTEM_STATE_ADMIN_LOGIN:
        case SYSTEM_STATE_ADMIN_INPUT:
        case SYSTEM_STATE_ADMIN_SCAN:
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

/* Checked here, locally; nothing about a PIN leaves the device except granted / denied. */
static void system_check_pin(system_effects_t *const p_fx)
{
    const uint8_t user = system_find_pin(s_sys.entry);

    system_entry_clear();
    if (0U != user)
    {
        ESP_LOGI(SYSTEM_TAG, "PIN accepted, user %u", user);
        system_grant(SYSTEM_METHOD_PIN, user, p_fx);
        return;
    }

    system_deny(SYSTEM_METHOD_PIN, p_fx);
}

/* A wrong PIN, card or finger. All three use one count: switching method does not give fresh tries. */
static void system_deny(system_method_t method, system_effects_t *const p_fx)
{
    static const char *const     s_names[] = {"PIN", "card", "finger"};
    static const system_notice_t s_notices[]
        = {SYSTEM_NOTICE_PIN_WRONG, SYSTEM_NOTICE_CARD_UNKNOWN, SYSTEM_NOTICE_FINGER_UNKNOWN};

    if (s_sys.fails < UINT8_MAX)
    {
        s_sys.fails++;
    }
    ESP_LOGW(SYSTEM_TAG, "Wrong %s (%u / %u)", s_names[method], s_sys.fails, SYSTEM_MAX_TRIES);
    p_fx->access        = true;
    p_fx->access_method = method;
    if (s_sys.fails >= SYSTEM_MAX_TRIES)
    {
        system_lockout(false, p_fx);
    }
    else
    {
        system_notify(s_notices[method], (uint8_t)(SYSTEM_MAX_TRIES - s_sys.fails));
    }
}

static void system_check_admin(system_effects_t *const p_fx)
{
    /* & rather than &&: both are compared, so the time taken does not tell which one was wrong. */
    const bool ok
        = system_text_equal(s_sys.login_id, SYSTEM_ADMIN_ID) & system_text_equal(s_sys.entry, SYSTEM_ADMIN_PASSWORD);

    if (ok)
    {
        ESP_LOGI(SYSTEM_TAG, "Admin login");
        s_sys.admin_fails = 0U;
        s_sys.lockouts    = 0U;
        system_enter(SYSTEM_STATE_ADMIN_MENU);
        return;
    }

    s_sys.admin_fails++;
    ESP_LOGW(SYSTEM_TAG, "Wrong admin login (%u / %u)", s_sys.admin_fails, SYSTEM_ADMIN_MAX_TRIES);
    if (s_sys.admin_fails >= SYSTEM_ADMIN_MAX_TRIES)
    {
        system_lockout(true, p_fx);
    }
    else
    {
        system_enter(SYSTEM_STATE_ADMIN_LOGIN); /* Both fields empty, back to the ID. */
        system_notify(SYSTEM_NOTICE_ADMIN_WRONG, (uint8_t)(SYSTEM_ADMIN_MAX_TRIES - s_sys.admin_fails));
    }
}

/* # on an admin Input step: user ID to add or delete, new PIN, PIN again. */
static void system_admin_submit(void)
{
    if ((SYSTEM_STEP_ADD_ID == s_sys.step) || (SYSTEM_STEP_DELETE_ID == s_sys.step))
    {
        const uint8_t user = system_parse_user(s_sys.entry);
        system_entry_clear();
        if (0U == user)
        {
            system_notify(SYSTEM_NOTICE_INVALID_ID, 0U);
            return;
        }

        const bool exists = system_user_has(s_sys.kind, user);
        if (SYSTEM_STEP_ADD_ID == s_sys.step)
        {
            if (exists)
            {
                system_notify(SYSTEM_NOTICE_ID_IN_USE, user);
                return;
            }
            s_sys.pending_user = user;
            s_sys.step         = SYSTEM_STEP_ADD_PIN;
            system_enter(SYSTEM_STATE_ADMIN_INPUT);
            return;
        }
        if (!exists)
        {
            system_notify(SYSTEM_NOTICE_NOT_FOUND, user);
            return;
        }
        system_user_delete(s_sys.kind, user);
        ESP_LOGI(SYSTEM_TAG, "Credential %d of user %u deleted", (int)s_sys.kind, user);
        system_enter(SYSTEM_STATE_CRED_MENU);
        system_notify(SYSTEM_NOTICE_DELETED, user);
        return;
    }

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
    if (SYSTEM_CRED_PIN == s_sys.kind)
    {
        s_sys.step = SYSTEM_STEP_ADD_ID;
        system_enter(SYSTEM_STATE_ADMIN_INPUT);
        return;
    }
    const uint8_t free_user = system_first_free(s_sys.kind);
    if (0U == free_user)
    {
        system_notify(SYSTEM_NOTICE_FULL, 0U);
        return;
    }
    system_enter(SYSTEM_STATE_ADMIN_SCAN);
    s_sys.scan_user    = free_user;
    s_sys.scan_count   = 0U;
    s_sys.scan_uid_len = 0U;
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
    const uint8_t user = s_sys.scan_user;
    system_enter(SYSTEM_STATE_CRED_MENU);
    system_notify(SYSTEM_NOTICE_SAVED, user);
    return true;
}

static void system_grant(system_method_t method, uint8_t user, system_effects_t *const p_fx)
{
    s_sys.fails          = 0U;
    s_sys.lockouts       = 0U;
    s_sys.lock_forever   = false;
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

static void system_lockout(bool admin, system_effects_t *const p_fx)
{
    if (s_sys.lockouts < UINT8_MAX)
    {
        s_sys.lockouts++;
    }
    s_sys.lock_forever = (s_sys.lockouts >= SYSTEM_MAX_LOCKOUTS);
    ESP_LOGW(SYSTEM_TAG,
             "%s lockout %u / %u%s",
             admin ? "Admin" : "PIN",
             s_sys.lockouts,
             SYSTEM_MAX_LOCKOUTS,
             s_sys.lock_forever ? ", locked until restart" : "");
    p_fx->alert      = true;
    p_fx->alert_code = admin ? SYSTEM_ALERT_ADMIN_LOCKOUT : SYSTEM_ALERT_PIN_LOCKOUT;
    system_enter(admin ? SYSTEM_STATE_ADMIN_LOCKOUT : SYSTEM_STATE_LOCKOUT);
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
        hooks.on_alert(p_fx->alert_code);
    }
    if (p_fx->voice && (NULL != hooks.on_voice))
    {
        hooks.on_voice(p_fx->voice_action);
    }
    if (p_fx->play && (NULL != hooks.on_play))
    {
        hooks.on_play();
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
    s_sys.notice       = notice;
    s_sys.notice_value = value;
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
    for (uint8_t i = 0U; i < SYSTEM_USER_COUNT; i++)
    {
        if ((s_card_len[i] == uid_length) && (0 == memcmp(s_cards[i], p_uid, uid_length)))
        {
            return (uint8_t)(i + 1U);
        }
    }
    return 0U;
}

/* 1..SYSTEM_USER_COUNT, or 0 when the text is not a valid user ID. */
static uint8_t system_parse_user(const char *p_text)
{
    char         *p_end = NULL;
    unsigned long value = strtoul(p_text, &p_end, 10);

    if ((p_end == p_text) || ('\0' != *p_end) || (value < 1UL) || (value > SYSTEM_USER_COUNT))
    {
        return 0U;
    }
    return (uint8_t)value;
}

static bool system_user_has(system_cred_kind_t kind, uint8_t user)
{
    switch (kind)
    {
        case SYSTEM_CRED_PIN:
            return '\0' != s_pins[user - 1U][0];
        case SYSTEM_CRED_CARD:
            return 0U != s_card_len[user - 1U];
        default:
            return s_fingers[user - 1U];
    }
}

static uint8_t system_first_free(system_cred_kind_t kind)
{
    for (uint8_t user = 1U; user <= SYSTEM_USER_COUNT; user++)
    {
        if (!system_user_has(kind, user))
        {
            return user;
        }
    }
    return 0U;
}

static uint8_t system_user_count(system_cred_kind_t kind)
{
    uint8_t count = 0U;

    for (uint8_t user = 1U; user <= SYSTEM_USER_COUNT; user++)
    {
        count += system_user_has(kind, user) ? 1U : 0U;
    }
    return count;
}

static void system_user_delete(system_cred_kind_t kind, uint8_t user)
{
    switch (kind)
    {
        case SYSTEM_CRED_PIN:
            memset(s_pins[user - 1U], 0, sizeof(s_pins[0]));
            break;
        case SYSTEM_CRED_CARD:
            memset(s_cards[user - 1U], 0, sizeof(s_cards[0]));
            s_card_len[user - 1U] = 0U;
            break;
        default:
            s_fingers[user - 1U] = false; /* TODO(fingerprint driver): also delete it in the sensor. */
            break;
    }
}
