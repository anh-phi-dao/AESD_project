#ifndef KEYPAD_CONFIG_H_
#define KEYPAD_CONFIG_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdint.h>

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

#ifndef KEYPAD_CFG_PARAMETER_CHECKING
#define KEYPAD_CFG_PARAMETER_CHECKING 1
#endif

#if (KEYPAD_CFG_PARAMETER_CHECKING != 0) && (KEYPAD_CFG_PARAMETER_CHECKING != 1)
#error "KEYPAD_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

#define KEYPAD_MAX_ROW_COUNT    4U
#define KEYPAD_MAX_COLUMN_COUNT 3U
#define KEYPAD_RELEASED_PATTERN UINT8_MAX
#define KEYPAD_IDLE_PATTERN     0xF0U

enum
{
    BUTTON_RELEASE,
    BUTTON_PRESSED,
    BUTTON_PRESSED_RELEASED,
    BUTTON_PRESSED_HOLD,
    BUTTON_HOLDING
};

enum
{
    RECORD = 20,
    PLAY,
    USER_MODE,
    ADMINSTRATOR_MODE,
    CHANGE_PASSWORD,
};

/* Temporary board mapping for the 4x3 keypad connected to the PCF8574. */
static const uint8_t s_keypad_row_masks[KEYPAD_MAX_ROW_COUNT] = {
    0x08U, /* ROW1 -> P3 */
    0x04U, /* ROW2 -> P2 */
    0x02U, /* ROW3 -> P1 */
    0x01U, /* ROW4 -> P0 */
};

static const uint8_t s_keypad_column_masks[KEYPAD_MAX_COLUMN_COUNT] = {
    0x10U, /* COL1 -> P4 */
    0x20U, /* COL2 -> P5 */
    0x40U, /* COL3 -> P6 */
};

static const char s_keypad_keymap[KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT] = {
    '1',
    '2',
    '3',
    '4',
    '5',
    '6',
    '7',
    '8',
    '9',
    '*',
    '0',
    '#',
};

static const char s_hold_keypad_keymap[KEYPAD_MAX_ROW_COUNT * KEYPAD_MAX_COLUMN_COUNT] = {
    '\0',
    '\0',
    '\0',
    '\0',
    '\0',
    '\0',
    '\0',
    RECORD,
    PLAY,
    USER_MODE,
    ADMINSTRATOR_MODE,
    CHANGE_PASSWORD,
};

#endif /* KEYPAD_CONFIG_H_ */
