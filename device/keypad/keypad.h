#ifndef KEYPAD_H_
#define KEYPAD_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "app_err.h"
#include "keypad_config.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef app_err_t (*keypad_port_write_t)(void *p_context, uint8_t value);
typedef app_err_t (*keypad_port_read_t)(void *p_context, uint8_t *p_value);

typedef struct st_keypad_port_api
{
    keypad_port_write_t p_write; /* Write a value to the keypad port. */
    keypad_port_read_t  p_read;  /* Read a value from the keypad port. */
} keypad_port_api_t;

typedef struct st_keypad_state
{
    uint16_t pressed_keys;   /* Bitmask of currently pressed keys. */
    uint8_t  state;          /* Pressed-Released or Pressed-Hold*/
    bool     ghost_detected; /* True if a ghost key press is detected. */
} keypad_state_t;

/* Configuration remains caller-owned and must stay valid and unchanged until deinit. */
typedef struct st_keypad_config
{
    const keypad_port_api_t *p_port_api;       /* Port API for reading and writing the keypad. */
    void                    *p_port_context;   /* Context passed to the port API functions. */
    const uint8_t           *p_row_masks;      /* Bitmask for each row. */
    const uint8_t           *p_column_masks;   /* Bitmask for each column. */
    const char              *p_keymap;         /* Key characters in row-major order. */
    uint8_t                  row_count;        /* Number of keypad rows. */
    uint8_t                  column_count;     /* Number of keypad columns. */
    uint8_t                  released_pattern; /* Port value with every keypad line released. */
    uint8_t                  idle_pattern;     /* Port value used while waiting for a key press. */
} keypad_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_keypad_instance_ctrl
{
    const keypad_config_t *p_cfg;         /* Caller-owned immutable configuration. */
    uint16_t               previous_keys; /* Key state from the previous scan. */
    uint16_t               stable_keys;   /* Debounced key state. */
    bool                   init;          /* True after successful initialization. */
} keypad_instance_ctrl_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Keep p_ctrl and p_cfg alive until deinit. Full descriptions are beside the implementations in keypad.c. */
#ifdef __cplusplus
extern "C"
{
#endif
    app_err_t keypad_init(keypad_instance_ctrl_t *const p_ctrl, const keypad_config_t *const p_cfg);

    app_err_t keypad_deinit(keypad_instance_ctrl_t *const p_ctrl);

    app_err_t keypad_scan(keypad_instance_ctrl_t *const p_ctrl, keypad_state_t *const p_state);

    char keypad_state_to_map_read(const keypad_state_t *const p_state);

#ifdef __cplusplus
}
#endif

#endif /* KEYPAD_H_ */
