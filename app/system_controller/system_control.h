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
#define SYSTEM_VOICE_MAX_MS    15000U
#define SYSTEM_MSG_IDLE_MS     20000U /* Message list of a guest: back to Home without keys. */
#define SYSTEM_MSG_SLOTS       5U     /* Messages from the owner kept for guests (web or recorded at the door). */
#define SYSTEM_REVIEW_IDLE_MS  30000U /* Review of a recording: sent as it is without keys. */
#define SYSTEM_PLAY_RECORDING  0xFFU  /* on_play slot: the recording under review, not a message for guests. */
#define SYSTEM_SCAN_TIMEOUT_MS 15000U /* Per scan: each good scan starts it again. */
#define SYSTEM_SCAN_COUNT      3U     /* Scans of the same card / finger that add it. */
#define SYSTEM_CARD_REPEAT_MS  1500U  /* The same card read again within this time is the same tap. */
#define SYSTEM_MAX_TRIES       5U     /* Wrong tries of one method (PIN, card or finger) before that method locks. */
#define SYSTEM_ADMIN_MAX_TRIES 3U

/* Lockouts of one method in a row, without a good unlock between them: each lasts longer, and the one after the last
 * step locks that method until an admin logs in. The other methods keep working meanwhile. */
#define SYSTEM_LOCKOUT_STEPS_MS {30000U, 60000U, 300000U, 900000U}
#define SYSTEM_ADMIN_LOCKOUT_MS 60000U     /* Admin login: never for good, it is what ends the other lockouts. */
#define SYSTEM_LOCK_FOREVER     UINT32_MAX /* Seconds left of a lockout that only an admin login ends. */
#define SYSTEM_USER_COUNT      20U /* User IDs 1..20. A user has one PIN, and any number of cards and fingers. */
#define SYSTEM_USER_TEMP       (SYSTEM_USER_COUNT + 1U) /* Who opened with a temporary guest code: "Temp code". */
#define SYSTEM_TEMP_CODE_LEN   8U /* An entry this long that is no PIN is checked as a temporary guest code. */
#define SYSTEM_CARD_SLOTS      20U /* Cards stored, whoever they belong to. */
#define SYSTEM_FINGER_SLOTS    40U /* Fingers stored; slot n is page n + 1 in the sensor. */
#define SYSTEM_FINGER_COUNT    10U /* Left thumb .. right little: see system_control_finger_name(). */
#define SYSTEM_FINGER_UNKNOWN  SYSTEM_FINGER_COUNT /* Finger stored before fingers had names. */
#define SYSTEM_LIST_MAX        SYSTEM_FINGER_SLOTS /* Longest admin list. */
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
    SYSTEM_STATE_VOICE,         /* Voice: recording a message. */
    SYSTEM_STATE_ADMIN_LOGIN,   /* AdminLogin: ID, then password. */
    SYSTEM_STATE_ADMIN_MENU,    /* Menu: ADMIN MENU. */
    SYSTEM_STATE_CRED_MENU,     /* Menu: PIN CODES / FINGERPRINTS / RFID CARDS. */
    SYSTEM_STATE_ADMIN_INPUT,   /* Input: new PIN, PIN again. */
    SYSTEM_STATE_ADMIN_SCAN,    /* Status: waiting for a finger or a card to add. */
    SYSTEM_STATE_ADMIN_LIST,    /* Menu as a scrolling list: pick a user, a finger, or what to delete. */
    SYSTEM_STATE_ADMIN_CONFIRM, /* Status: delete this entry? */
    SYSTEM_STATE_MSG_LIST,      /* Menu as a list of the owner's messages: a guest, or the admin. */
    SYSTEM_STATE_PLAYING,       /* Voice: playing a message from the owner. */
    SYSTEM_STATE_MSG_MENU,      /* Menu: MESSAGES FOR GUESTS (admin). */
    SYSTEM_STATE_MSG_DELETE,    /* Status: delete this message? (admin) */
    SYSTEM_STATE_VOICE_REVIEW,  /* Status: recording done, listen / record again / send. */
} system_state_t;

/* What ADMIN_LIST is for. */
typedef enum e_system_list_purpose
{
    SYSTEM_LIST_PICK_USER = 0, /* Add: whose PIN / card / finger. */
    SYSTEM_LIST_PICK_FINGER,   /* Add finger: which finger of that user. */
    SYSTEM_LIST_DELETE,        /* Delete: every entry of the kind, with its owner. */
} system_list_purpose_t;

/* One row of ADMIN_LIST. The screen writes the text from it (system_control_user_name / finger_name). */
typedef struct st_system_list_item
{
    uint8_t  user;     /* Owner, 1..SYSTEM_USER_COUNT. */
    uint8_t  finger;   /* PICK_FINGER, DELETE of a finger: 0..SYSTEM_FINGER_UNKNOWN. */
    uint8_t  count;    /* PICK_USER: entries of the kind that user already has. */
    uint8_t  slot;     /* DELETE of a card / finger: its slot. */
    uint16_t uid_tail; /* DELETE of a card: last two bytes of the UID, to tell two cards apart. */
} system_list_item_t;

/* Where a message for guests came from. */
typedef enum e_system_msg_source
{
    SYSTEM_MSG_FROM_DOOR = 0, /* Recorded by the admin at the door. */
    SYSTEM_MSG_FROM_WEB,      /* Sent by the owner from the web page. */
} system_msg_source_t;

/* One message from the owner, as the guest list shows it. Guests never get the messages of other guests. */
typedef struct st_system_message
{
    uint8_t  slot;    /* File of the voice code, 0..SYSTEM_MSG_SLOTS - 1. */
    uint8_t  source;  /* system_msg_source_t. */
    uint8_t  seconds; /* Length, rounded up. */
    uint32_t ts;      /* Unix time it arrived, 0 when the clock was not set. */
} system_message_t;

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
    SYSTEM_STEP_ADD_PIN = 0, /* New PIN for the user picked in the list. */
    SYSTEM_STEP_ADD_CONFIRM, /* New PIN again. */
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
    SYSTEM_NOTICE_VOICE_CANCELLED,
    SYSTEM_NOTICE_NO_MESSAGES,
    SYSTEM_NOTICE_MSG_SENT,        /* A guest's message is kept for the owner. */
    SYSTEM_NOTICE_MSG_SAVED,       /* A message for guests is kept. */
    SYSTEM_NOTICE_MSG_TOO_SHORT,   /* Under a second: dropped. */
    SYSTEM_NOTICE_MSG_FAILED,      /* Microphone or SD card error. */
    SYSTEM_NOTICE_MSG_UNAVAILABLE, /* No SD card: messages cannot be kept. */
    SYSTEM_NOTICE_MSG_FULL,        /* SYSTEM_MSG_SLOTS messages for guests already. */
    SYSTEM_NOTICE_MSG_DELETED,
    SYSTEM_NOTICE_PLAY_FAILED,
    SYSTEM_NOTICE_EMPTY,   /* Delete: nothing of this kind is stored. */
    SYSTEM_NOTICE_DELETED, /* value: user ID, value2: finger (fingers only). */
    SYSTEM_NOTICE_PIN_TOO_SHORT,
    SYSTEM_NOTICE_PIN_USED,
    SYSTEM_NOTICE_PIN_MISMATCH,
    SYSTEM_NOTICE_SAVED,         /* value: user ID, value2: finger (fingers only). */
    SYSTEM_NOTICE_FULL,          /* No free slot left (or every user already has a PIN). */
    SYSTEM_NOTICE_FINGER_IN_USE, /* value: user ID, value2: finger, of the template it matched. */
    SYSTEM_NOTICE_ALL_FINGERS,   /* value: user ID, who already has all ten fingers. */
    SYSTEM_NOTICE_CARD_IN_USE,   /* value: user ID that has it. */
    SYSTEM_NOTICE_SCAN_TIMEOUT,  /* The reader saw nothing; kind tells which. */
    SYSTEM_NOTICE_SCAN_MISMATCH, /* Another card than the first scan: start again. */
    SYSTEM_NOTICE_SCAN_BAD,      /* The sensor could not read the finger: scan again. */
    SYSTEM_NOTICE_LOCKED, /* value: system_lock_target_t, value2: 1 it just locked, 0 it was used while locked.
                           * system_view_t.notice_lock_s: seconds left, or SYSTEM_LOCK_FOREVER. */
    SYSTEM_NOTICE_TEMP_NO_TIME, /* A temporary code could not be checked: the clock is not set. value: tries left. */
} system_notice_t;

/* Everything the screen needs, copied by system_control_get_view(). Never holds a PIN or password: only lengths. */
typedef struct st_system_view
{
    system_state_t        state;
    system_cred_kind_t    kind; /* CRED_MENU, ADMIN_INPUT, ADMIN_SCAN. */
    system_admin_step_t   step; /* ADMIN_INPUT. */
    bool                  door_unlocked;
    bool                  login_on_password;                 /* ADMIN_LOGIN: the password field has the keys. */
    uint8_t               entry_len;                         /* Characters in the field that has the keys. */
    char                  entry_text[SYSTEM_ENTRY_MAX + 1U]; /* That field when it is not secret (IDs), else "". */
    char                  login_id[SYSTEM_ENTRY_MAX + 1U];   /* ADMIN_LOGIN: the ID typed. */
    uint8_t               stored_count;                      /* CRED_MENU: entries of kind. */
    uint8_t               message_count;                     /* Messages from the owner for guests (Home). */
    bool                  msg_admin;                         /* VOICE, MSG_LIST, PLAYING: for the admin, not a guest. */
    uint8_t               review_seconds; /* VOICE_REVIEW: length of the recording, 0 until the voice code says. */
    uint8_t               play_seconds;   /* PLAYING: length of the message. */
    uint32_t              state_ms;       /* Time in this state. */
    uint32_t              notice_seq;     /* Changes with every new notice. */
    system_notice_t       notice;
    uint8_t               notice_value;
    uint8_t               notice_value2;
    uint32_t              notice_lock_s;  /* NOTICE_LOCKED: seconds left, or SYSTEM_LOCK_FOREVER. */
    uint8_t               pending_user;   /* ADMIN_INPUT, ADMIN_SCAN, ADMIN_CONFIRM: the user picked. */
    uint8_t               pending_finger; /* Finger ADMIN_SCAN / ADMIN_CONFIRM: the finger picked. */
    uint8_t               scan_slot;      /* ADMIN_SCAN: slot it goes to; a finger is stored at page scan_slot + 1. */
    uint8_t               scan_count;     /* ADMIN_SCAN: good scans so far, 0..SYSTEM_SCAN_COUNT - 1. */
    system_list_purpose_t list_purpose;   /* ADMIN_LIST. */
    uint8_t               list_count;
    uint8_t               list_sel; /* Highlighted row. */
    system_list_item_t    list[SYSTEM_LIST_MAX];
    system_message_t      messages[SYSTEM_MSG_SLOTS]; /* MSG_LIST rows (list_count, list_sel), newest first. */
} system_view_t;

typedef enum e_system_method
{
    SYSTEM_METHOD_PIN = 0,
    SYSTEM_METHOD_CARD,
    SYSTEM_METHOD_FINGER,
} system_method_t;

/* What one lockout applies to: a method, or the admin login. Each has its own wrong tries and lockouts. */
typedef enum e_system_lock_target
{
    SYSTEM_LOCK_PIN = SYSTEM_METHOD_PIN,
    SYSTEM_LOCK_CARD = SYSTEM_METHOD_CARD,
    SYSTEM_LOCK_FINGER = SYSTEM_METHOD_FINGER,
    SYSTEM_LOCK_ADMIN,
    SYSTEM_LOCK_COUNT
} system_lock_target_t;

/* Answer of check_temp_code. */
typedef enum e_system_temp_code
{
    SYSTEM_TEMP_CODE_WRONG = 0, /* Not valid now, or temporary codes are off. */
    SYSTEM_TEMP_CODE_OK,
    SYSTEM_TEMP_CODE_NO_TIME, /* The clock is not set yet: no code can be checked. */
} system_temp_code_t;

typedef enum e_system_alert
{
    SYSTEM_ALERT_LOCKOUT = 0, /* on_alert target and lock_s say which one and for how long. */
    SYSTEM_ALERT_UNKNOWN_CARD,
} system_alert_t;

typedef enum e_system_voice
{
    SYSTEM_VOICE_START = 0,
    SYSTEM_VOICE_STOP,   /* Stop recording, keep it for the review. */
    SYSTEM_VOICE_CANCEL, /* Drop it: while recording or in the review. */
    SYSTEM_VOICE_SEND,   /* Review done: keep it (guest: send it to the owner). */
} system_voice_t;

/* Every stored credential. For storage only: holds PINs in clear, so never log it. */
typedef struct st_system_credentials
{
    char    pins[SYSTEM_USER_COUNT][SYSTEM_ENTRY_MAX + 1U]; /* By user ID - 1. "": no PIN. */
    uint8_t card_user[SYSTEM_CARD_SLOTS];                   /* By slot. 0: free slot. */
    uint8_t cards[SYSTEM_CARD_SLOTS][SYSTEM_UID_MAX];
    uint8_t card_len[SYSTEM_CARD_SLOTS];
    uint8_t finger_user[SYSTEM_FINGER_SLOTS]; /* By slot. 0: free. The template is in the sensor at page slot + 1. */
    uint8_t finger_id[SYSTEM_FINGER_SLOTS];   /* 0..SYSTEM_FINGER_UNKNOWN. */
} system_credentials_t;

/* What the rest of the firmware does about a decision. Called from the task that caused it (keypad, rfid or
 * system_control_task), outside the system_control lock. Every member may be NULL. */
typedef struct st_system_control_hooks
{
    void (*on_access)(system_method_t method, bool granted, uint8_t user); /* user: 0 when denied. */
    /* target, lock_s: ALERT_LOCKOUT only; lock_s in seconds, or SYSTEM_LOCK_FOREVER. */
    void (*on_alert)(system_alert_t alert, system_lock_target_t target, uint32_t lock_s);
    void (*on_locks_forever)(uint8_t mask);              /* Bit n: target n locked until an admin login. Keep it. */
    void (*on_door)(bool unlocked);                      /* Drive the solenoid. */
    void (*on_voice)(system_voice_t action, bool owner); /* owner: a message for guests, else one for the owner. */
    void (*on_play)(uint8_t slot, uint8_t token);        /* Play this message from the owner; token for play_done. */
    void (*on_play_stop)(void);
    void (*on_message_delete)(uint8_t slot); /* Delete this message for guests. */
    void (*on_finger_delete)(uint8_t page);  /* Delete this template in the sensor. */
    /* Unlike the others, called under the lock, for a SYSTEM_TEMP_CODE_LEN entry that is no PIN: must be quick and
     * must not call system_control. NULL: no temporary codes. */
    system_temp_code_t (*check_temp_code)(const char *p_code);
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

/* The fingerprint sensor matched the template at this page (1..SYSTEM_FINGER_SLOTS), or none (0). */
void system_control_finger(uint8_t page);

/* ADMIN_SCAN for a fingerprint: the sensor took one image (good) or could not (false). The driver stores the template
 * at page system_view_t.scan_slot + 1; the SYSTEM_SCAN_COUNT-th good image adds the finger. For the fingerprint driver.
 */
void system_control_finger_scanned(bool good);

/* ADMIN_SCAN for a fingerprint: scan 1 matched the stored template at page. True when that template belongs to someone
 * (the add stops, "already added"); false when its slot is free, a leftover the driver should delete before going on.
 */
bool system_control_finger_scan_duplicate(uint8_t page);

/* ADMIN_SCAN for a fingerprint: the scans were not the same finger. Counting starts again at scan 1. */
void system_control_finger_scan_restart(void);

/* Timeouts and countdowns. Call every 100 ms or so. */
void system_control_tick(void);

/* At boot: the methods on_locks_forever reported locked until an admin login, so a restart does not end them. */
void system_control_set_locks_forever(uint8_t mask);

/* From the voice code: whether messages can be kept (SD card), and the owner's messages for guests, newest first.
 * Home shows the count; hold # plays the only one, or lists them. */
void system_control_set_messages(bool ready, const system_message_t *p_list, uint8_t count);

/* From the voice code: how a recording ended (MSG_SENT, MSG_SAVED, MSG_TOO_SHORT, MSG_FAILED). A failure leaves the
 * recording or review screen if it is still shown. */
void system_control_voice_result(system_notice_t notice);

/* From the voice code: the recording stopped by SYSTEM_VOICE_STOP is ready for the review, this long. */
void system_control_voice_recorded(uint8_t seconds);

/* From the voice code: the message started with this token ended (ok), or could not be played. Not called when
 * on_play_stop stopped it. A token of an older message is ignored. */
void system_control_play_done(uint8_t token, bool ok);

/* Copy of the state for the screen. False when the lock could not be taken in time. */
bool system_control_get_view(system_view_t *const p_view);

/* "Dat", "Phi", "Tuan", then "User 4" ... "User 20", and "Temp code" for SYSTEM_USER_TEMP. Valid until the next call
 * for a numbered user. */
const char *system_control_user_name(uint8_t user);

/* "Left thumb" .. "Right little", or "Finger" for SYSTEM_FINGER_UNKNOWN. */
const char *system_control_finger_name(uint8_t finger);

/* Changes with every PIN, card or finger added or deleted from the admin menu. One word read: cheap to poll. */
uint32_t system_control_credentials_seq(void);

/* Copy of every credential, and the credentials_seq it matches. False when the lock could not be taken in time. */
bool system_control_get_credentials(system_credentials_t *const p_cred, uint32_t *const p_seq);

/* Replaces every credential (the DEV DEFAULTS too), e.g. with what storage read at boot. Entries that break the rules
 * of the admin menu (non-digit or short PIN, PIN or card of another user, card too long, unknown user, a finger twice)
 * are dropped and logged. Does not change credentials_seq: what was just loaded needs no saving. */
app_err_t system_control_set_credentials(const system_credentials_t *const p_cred);

#endif
