#ifndef UI_TASK_H
#define UI_TASK_H

#include <stdint.h>
#include "keypad.h"
#include "sdkconfig.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Same default as the driver config headers; LVGL and the UI need FreeRTOS. */
#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if USED_FREERTOS && CONFIG_UI_ENABLE
#define UI_TASK_ENABLED 1
#else
#define UI_TASK_ENABLED 0
#endif

/***********************************************************************************************************************
 * Brings up the screen: ILI9341 (device/ili9341_tft_display) <- lvgl_port <- ui_service (SquareLine screens).
 * A missing or broken display is logged and ignored: the lock must keep working without it.
 * Does nothing when the UI is disabled in menuconfig.
 **********************************************************************************************************************/
void boot_ui(void);

/***********************************************************************************************************************
 * Sends keys that went down since the previous scan to the focused LVGL object. Holding a key does not repeat it.
 * Does nothing before boot_ui() succeeded or when the UI is disabled.
 **********************************************************************************************************************/
void ui_task_post_new_keys(uint16_t pressed_keys, uint16_t previous_keys);

#endif
