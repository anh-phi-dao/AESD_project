#ifndef UI_FLOW_TASK_H
#define UI_FLOW_TASK_H

#include <stdint.h>
#include "ui_task.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define UI_FLOW_TASK_STACK      4096U
#define UI_FLOW_TASK_PRIORITY   2U
#define UI_FLOW_TICK_MS         100U /* How often system_control is read and the screen follows it. */
#define UI_FLOW_LOCK_TIMEOUT_MS 200U
#define UI_FLOW_POPUP_MS        2000U

/***********************************************************************************************************************
 * Starts the screen of docs/ui_data.md. It only shows what system_control decides: keypad_task and rfid_task feed
 * system_control, this task reads system_control_get_view() every UI_FLOW_TICK_MS and draws it. Nothing here checks a
 * code or opens the door. Call after boot_system_control() and boot_ui(). Does nothing without a screen or without a
 * SquareLine export.
 **********************************************************************************************************************/
void boot_ui_flow(void);

#endif
