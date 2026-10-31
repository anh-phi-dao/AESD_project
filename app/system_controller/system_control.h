#ifndef SYSTEM_MODE_H
#define SYSTEM_MODE_H

#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#include <stdbool.h>
#include <stdint.h>

#include "app_err.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

/* Keys, timings and limits of docs/ui_data.md. */
#define SYSTEM_HOLD_MS         3000U  /* A key held this long is a hold, not a press (keypad_task). */
#define SYSTEM_PIN_IDLE_MS     10000U /* Input: back to Home without keys. */
#define SYSTEM_LOGIN_IDLE_MS   20000U /* AdminLogin: back to Home without keys. */
#define SYSTEM_ADMIN_IDLE_MS   30000U /* Admin screens: admin mode ends without keys. */
#define SYSTEM_UNLOCK_MS       5000U  /* Door stays unlocked. */
#define SYSTEM_LOCKOUT_MS      60000U
#define SYSTEM_VOICE_MAX_MS    15000U
#define SYSTEM_SCAN_TIMEOUT_MS 15000U /* Per scan: each good scan starts it again. */
#define SYSTEM_SCAN_COUNT      3U     /* Scans of the same card / finger that add it. */
#define SYSTEM_CARD_REPEAT_MS  1500U  /* The same card read again within this time is the same tap. */
#define SYSTEM_MAX_TRIES       5U     /* Wrong PINs, unknown cards and unknown fingers together. */
#define SYSTEM_ADMIN_MAX_TRIES 3U
#define SYSTEM_MAX_LOCKOUTS    10U /* Lockouts in a row, without a good code between them, lock until restart. */
#define SYSTEM_USER_COUNT      20U /* User IDs 1..20 for PINs, fingerprints and cards. */
#define SYSTEM_PIN_MIN_LEN     4U
#define SYSTEM_ENTRY_MAX       8U /* Digits in a PIN, user ID or admin ID / password: the screen fields take 8. */
#define SYSTEM_UID_MAX         8U

/* DEV DEFAULTS until storage keeps credentials in flash: everything added from the admin menu is lost on restart.
 * Change these before a real install. */
#ifndef SYSTEM_DEFAULT_PIN
#define SYSTEM_DEFAULT_PIN "1234567" /* User 1. */
#endif
#ifndef SYSTEM_ADMIN_ID
#define SYSTEM_ADMIN_ID "1"
#endif
#ifndef SYSTEM_ADMIN_PASSWORD
#define SYSTEM_ADMIN_PASSWORD "1234"
#endif

/*******************************************************************************************************************
 * Typedef definitions
 ******************************************************************************************************************/

/* One state per screen of docs/ui_data.md; the screen shows the state, the keys act on it. */
typedef enum e_system_state
{
    SYSTEM_STATE_HOME = 0,      /* Home. */
    SYSTEM_STATE_PIN_ENTRY,     /* Input: ENTER PIN. */
    SYSTEM_STATE_LOCKOUT,       /* Status: too many wrong PINs. */
    SYSTEM_STATE_VOICE,         /* Voice: recording a message. */
    SYSTEM_STATE_ADMIN_LOGIN,   /* AdminLogin: ID, then password. */
    SYSTEM_STATE_ADMIN_LOCKOUT, /* Status: too many wrong admin logins. */
    SYSTEM_STATE_ADMIN_MENU,    /* Menu: ADMIN MENU. */
    SYSTEM_STATE_CRED_MENU,     /* Menu: PIN CODES / FINGERPRINTS / RFID CARDS. */
    SYSTEM_STATE_ADMIN_INPUT,   /* Input: user ID, new PIN, PIN again. */
    SYSTEM_STATE_ADMIN_SCAN,    /* Status: waiting for a finger or a card to add. */
} system_state_t;

/* What an admin sub menu manages; also its row in ADMIN MENU minus one. */
typedef enum e_system_cred_kind
{
    SYSTEM_CRED_PIN = 0,
    SYSTEM_CRED_FINGER,
    SYSTEM_CRED_CARD,
    SYSTEM_CRED_COUNT
} system_cred_kind_t;

typedef enum e_system_admin_step
{
    SYSTEM_STEP_ADD_ID = 0,  /* Add PIN: user ID first. Cards and fingers take the first free ID instead. */
    SYSTEM_STEP_ADD_PIN,     /* New PIN; the user ID was taken first. */
    SYSTEM_STEP_ADD_CONFIRM, /* New PIN again. */
    SYSTEM_STEP_DELETE_ID,
} system_admin_step_t;

/* One-off messages for the screen (a popup). value: see each code. */
typedef enum e_system_notice
{
    SYSTEM_NOTICE_NONE = 0,
    SYSTEM_NOTICE_UNLOCKED,       /* value: user ID. */
    SYSTEM_NOTICE_PIN_WRONG,      /* value: tries left. */
    SYSTEM_NOTICE_CARD_UNKNOWN,   /* value: tries left. */
    SYSTEM_NOTICE_FINGER_UNKNOWN, /* value: tries left. */
    SYSTEM_NOTICE_ADMIN_WRONG,    /* value: tries left. */
    SYSTEM_NOTICE_ADMIN_TIMEOUT,
    SYSTEM_NOTICE_VOICE_STOPPED,
    SYSTEM_NOTICE_VOICE_CANCELLED,
    SYSTEM_NOTICE_NO_MESSAGES,
    SYSTEM_NOTICE_PLAYING,
    SYSTEM_NOTICE_INVALID_ID,
    SYSTEM_NOTICE_ID_IN_USE, /* value: user ID. */
    SYSTEM_NOTICE_NOT_FOUND, /* value: user ID. */
    SYSTEM_NOTICE_DELETED,   /* value: user ID. */
    SYSTEM_NOTICE_PIN_TOO_SHORT,
    SYSTEM_NOTICE_PIN_USED,
    SYSTEM_NOTICE_PIN_MISMATCH,
    SYSTEM_NOTICE_SAVED,         /* value: user ID. */
    SYSTEM_NOTICE_FULL,          /* No free user ID left. */
    SYSTEM_NOTICE_CARD_IN_USE,   /* value: user ID that has it. */
    SYSTEM_NOTICE_SCAN_TIMEOUT,  /* The reader saw nothing; kind tells which. */
    SYSTEM_NOTICE_SCAN_MISMATCH, /* Another card than the first scan: start again. */
    SYSTEM_NOTICE_SCAN_BAD,      /* The sensor could not read the finger: scan again. */
} system_notice_t;

/* Everything the screen needs, copied by system_control_get_view(). Never holds a PIN or password: only lengths. */
typedef struct st_system_view
{
    system_state_t      state;
    system_cred_kind_t  kind; /* CRED_MENU, ADMIN_INPUT, ADMIN_SCAN. */
    system_admin_step_t step; /* ADMIN_INPUT. */
    bool                door_unlocked;
    bool                lock_forever;                      /* Lockout ends only with a restart. */
    bool                login_on_password;                 /* ADMIN_LOGIN: the password field has the keys. */
    uint8_t             entry_len;                         /* Characters in the field that has the keys. */
    char                entry_text[SYSTEM_ENTRY_MAX + 1U]; /* That field when it is not secret (IDs), else "". */
    char                login_id[SYSTEM_ENTRY_MAX + 1U];   /* ADMIN_LOGIN: the ID typed. */
    uint8_t             stored_count;                      /* CRED_MENU: entries of kind. */
    uint8_t             message_count;                     /* Voice messages waiting (Home). */
    uint32_t            state_ms;                          /* Time in this state. */
    uint32_t            notice_seq;                        /* Changes with every new notice. */
    system_notice_t     notice;
    uint8_t             notice_value;
    uint8_t             scan_user;  /* ADMIN_SCAN: user ID the card / finger will be saved to. */
    uint8_t             scan_count; /* ADMIN_SCAN: good scans so far, 0..SYSTEM_SCAN_COUNT - 1. */
} system_view_t;

typedef enum e_system_method
{
    SYSTEM_METHOD_PIN = 0,
    SYSTEM_METHOD_CARD,
    SYSTEM_METHOD_FINGER,
} system_method_t;

typedef enum e_system_alert
{
    SYSTEM_ALERT_PIN_LOCKOUT = 0,
    SYSTEM_ALERT_ADMIN_LOCKOUT,
    SYSTEM_ALERT_UNKNOWN_CARD,
} system_alert_t;

typedef enum e_system_voice
{
    SYSTEM_VOICE_START = 0,
    SYSTEM_VOICE_STOP,   /* Keep the message. */
    SYSTEM_VOICE_CANCEL, /* Drop it. */
} system_voice_t;

/* What the rest of the firmware does about a decision. Called from the task that caused it (keypad, rfid or
 * system_control_task), outside the system_control lock. Every member may be NULL. */
typedef struct st_system_control_hooks
{
    void (*on_access)(system_method_t method, bool granted, uint8_t user); /* user: 0 when denied. */
    void (*on_alert)(system_alert_t alert);
    void (*on_door)(bool unlocked); /* Drive the solenoid. */
    void (*on_voice)(system_voice_t action);
    void (*on_play)(void);                  /* Play the newest message from the owner. */
    void (*on_finger_delete)(uint8_t user); /* Delete the template of this user in the sensor. */
} system_control_hooks_t;

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/* Starts in Home with the DEV DEFAULTS credentials. hooks is copied; NULL means no hooks. */
app_err_t system_control_init(const system_control_hooks_t *const p_hooks);

/* A key of the 4x3 keypad ('0'-'9', '*', '#'): pressed and released (hold false), or held SYSTEM_HOLD_MS (hold
 * true; its release is not reported). */
void system_control_key(char key, bool hold);

/* A card was read. A card read again within SYSTEM_CARD_REPEAT_MS counts once. */
void system_control_card(const uint8_t *p_uid, uint8_t uid_length);

/* The fingerprint sensor matched a finger to this user ID, or matched none (0). For the fingerprint driver. */
void system_control_finger(uint8_t user);

/* ADMIN_SCAN for a fingerprint: the sensor took one image (good) or could not (false). The driver stores the template
 * under system_view_t.scan_user; the SYSTEM_SCAN_COUNT-th good image adds the finger. For the fingerprint driver. */
void system_control_finger_scanned(bool good);

/* ADMIN_SCAN for a fingerprint: the scans were not the same finger. Counting starts again at scan 1. */
void system_control_finger_scan_restart(void);

/* Timeouts and countdowns. Call every 100 ms or so. */
void system_control_tick(void);

/* False while the keypad is locked out: keypad_task may stop scanning. */
bool system_control_keys_enabled(void);

/* Voice messages waiting, from the audio code. Shown on Home; hold # plays the newest. */
void system_control_set_message_count(uint8_t count);

/* Copy of the state for the screen. False when the lock could not be taken in time. */
bool system_control_get_view(system_view_t *const p_view);

#endif
