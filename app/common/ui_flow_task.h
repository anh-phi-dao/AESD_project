#ifndef UI_FLOW_TASK_H
#define UI_FLOW_TASK_H

#include <stdint.h>
#include "ui_task.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_FLOW_TASK_STACK      4096U
#define UI_FLOW_TASK_PRIORITY   2U
#define UI_FLOW_TICK_MS         100U /* Hold, timeout and countdown resolution. */
#define UI_FLOW_LOCK_TIMEOUT_MS 200U

#define UI_FLOW_HOLD_MS        3000U /* Hold * / # / 0 on Home, hold * to cancel. */
#define UI_FLOW_PIN_IDLE_MS    10000U
#define UI_FLOW_LOGIN_IDLE_MS  20000U
#define UI_FLOW_ADMIN_IDLE_MS  30000U /* Admin mode ends without keys for this long. */
#define UI_FLOW_UNLOCK_SHOW_MS 5000U  /* Home shows UNLOCKED this long. The solenoid is not driven yet. */
#define UI_FLOW_LOCKOUT_S      60U
#define UI_FLOW_VOICE_MAX_S    15U
#define UI_FLOW_SCAN_TIMEOUT_S 15U
#define UI_FLOW_POPUP_MS       2000U

#define UI_FLOW_PIN_MAX_TRIES   5U
#define UI_FLOW_ADMIN_MAX_TRIES 3U
#define UI_FLOW_USER_COUNT      20U /* User IDs 1..20 for PINs, fingerprints and cards. */
#define UI_FLOW_PIN_MIN_LEN     4U

/* DEV DEFAULTS until storage_manager keeps credentials in flash: PINs added from the admin menu live in RAM and are
 * lost on reset. User 1 starts with this PIN (the one keypad_task used before). Change both before a real install. */
#ifndef UI_FLOW_DEFAULT_PIN
#define UI_FLOW_DEFAULT_PIN "1234567"
#endif
#ifndef UI_FLOW_ADMIN_ID
#define UI_FLOW_ADMIN_ID "1"
#endif
#ifndef UI_FLOW_ADMIN_PASSWORD
#define UI_FLOW_ADMIN_PASSWORD "1234"
#endif

/***********************************************************************************************************************
 * Starts the screen flow of docs/ui_data.md: which screen follows which key, local PIN and admin checks, lockout,
 * timeouts, top bar. Call after boot_ui(). Does nothing without a screen or without a SquareLine export.
 **********************************************************************************************************************/
void boot_ui_flow(void);

/***********************************************************************************************************************
 * Tells the flow which keys went down or up since the previous scan. Call from keypad_task after
 * ui_task_post_new_keys(). Never blocks; does nothing before boot_ui_flow() succeeded.
 **********************************************************************************************************************/
void ui_flow_post_keys(uint16_t pressed_keys, uint16_t previous_keys);

#endif
