#ifndef UI_SERVICE_CONFIG_H_
#define UI_SERVICE_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init ordering from the caller. */
#ifndef UI_SERVICE_CFG_PARAMETER_CHECKING
#define UI_SERVICE_CFG_PARAMETER_CHECKING 1
#endif

#if (UI_SERVICE_CFG_PARAMETER_CHECKING != 0) && (UI_SERVICE_CFG_PARAMETER_CHECKING != 1)
#error "UI_SERVICE_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* The 4x3 keypad has no arrow keys. Digits are typed as they are; these two act as LVGL control keys. */
#define UI_SERVICE_KEY_CHAR_ENTER         '#' /* LV_KEY_ENTER: confirm, e.g. submit a PIN text area. */
#define UI_SERVICE_KEY_CHAR_BACKSPACE     '*' /* LV_KEY_BACKSPACE: delete the last digit. */

/* Building the first screen happens before anything else needs the lock, so this only guards against a hang. */
#define UI_SERVICE_INIT_LOCK_TIMEOUT_MS   1000U

/* A screen change only waits for the LVGL task to finish its current frame. */
#define UI_SERVICE_SCREEN_LOCK_TIMEOUT_MS 200U

/* Popup background per level (0xRRGGBB). The error color matches the Popup panel drawn in SquareLine. */
#define UI_SERVICE_POPUP_COLOR_INFO       0x1C2630U
#define UI_SERVICE_POPUP_COLOR_SUCCESS    0x27AE60U
#define UI_SERVICE_POPUP_COLOR_ERROR      0xC0392BU

#endif /* UI_SERVICE_CONFIG_H_ */
