#ifndef UI_FLOW_TASK_H
#define UI_FLOW_TASK_H

#include <stdint.h>
#include "ui_task.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* LVGL calls nest deep on a screen change, and the list rows use snprintf / strftime: 4 KB overflowed in -O0 builds. */
#define UI_FLOW_TASK_STACK      8192U
#define UI_FLOW_TASK_PRIORITY   2U
#define UI_FLOW_TICK_MS         100U /* How often system_control is read and the screen follows it. */
#define UI_FLOW_LOCK_TIMEOUT_MS 200U
#define UI_FLOW_POPUP_MS        2000U
#define UI_FLOW_BATTERY_UNKNOWN 0xFFU /* ui_flow_set_battery(): no reading yet, the top bar shows "--%". */

/***********************************************************************************************************************
 * Starts the screen of docs/ui_data.md. It only shows what system_control decides: keypad_task and rfid_task feed
 * system_control, this task reads system_control_get_view() every UI_FLOW_TICK_MS and draws it. Nothing here checks a
 * code or opens the door. Call after boot_system_control() and boot_ui(). Does nothing without a screen or without a
 * SquareLine export.
 **********************************************************************************************************************/
void boot_ui_flow(void);

/***********************************************************************************************************************
 * For the battery code: the charge shown in the top bar, 0..100 % (more counts as 100), or UI_FLOW_BATTERY_UNKNOWN.
 * Any task, any time, without a lock; drawn within UI_FLOW_TICK_MS. Until it is called the top bar shows "--%".
 **********************************************************************************************************************/
void ui_flow_set_battery(uint8_t percent);

/* The last value given to ui_flow_set_battery(), UI_FLOW_BATTERY_UNKNOWN before the first. */
uint8_t ui_flow_get_battery(void);

#endif
