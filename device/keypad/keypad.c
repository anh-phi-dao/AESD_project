/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <limits.h>
#include <stddef.h>

#include "keypad.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

#if KEYPAD_CFG_PARAMETER_CHECKING
static bool keypad_is_single_bit(uint8_t mask);
#endif
static app_err_t keypad_validate_init_args(keypad_instance_ctrl_t *const p_ctrl, const keypad_config_t *const p_cfg);
static app_err_t keypad_scan_row(const keypad_config_t *const p_cfg, uint8_t row_index, uint16_t *const p_row_keys);
static bool      keypad_is_ghost_detected(const keypad_config_t *const p_cfg, uint16_t pressed_keys);
/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Initializes keypad state using the caller-provided port interface and matrix configuration.
 *
 * * Initializes keypad state and places the configured matrix in its idle state.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged until deinit.
 *
 * @retval APP_SUCCESS               Initialization succeeded.
 * @retval APP_ERR_INVALID_POINTER   A required API argument is NULL.
 * @retval APP_ERR_INVALID_STATE     The control block is already initialized.
 * @retval APP_ERR_INVALID_ARGUMENT  A required configuration member is invalid.
 * @return                           An error returned by the configured port write callback.
 **********************************************************************************************************************/
app_err_t keypad_init(keypad_instance_ctrl_t *const p_ctrl, const keypad_config_t *const p_cfg)
{
    app_err_t ret = keypad_validate_init_args(p_ctrl, p_cfg);
    if (APP_SUCCESS != ret)
    {
        return ret;
    }

    /* Arm the matrix by driving all rows LOW while leaving all columns released HIGH. */
    ret = p_cfg->p_port_api->p_write(p_cfg->p_port_context, p_cfg->idle_pattern);
    if (APP_SUCCESS != ret)
    {
        return ret;
    }

    /* Commit the caller-owned configuration only after the hardware reaches its idle state. This keeps the control
     * block reusable if the port write fails. */
    p_ctrl->p_cfg         = p_cfg;
    p_ctrl->previous_keys = 0U;
    p_ctrl->stable_keys   = 0U;
    p_ctrl->init          = true;

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * De-initializes keypad state without releasing the caller-owned port device.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval APP_SUCCESS              The keypad lines were released and the control block was reset.
 * @retval APP_ERR_INVALID_POINTER  The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE    The control block is not initialized.
 * @return                          An error returned by the configured port write callback.
 **********************************************************************************************************************/
app_err_t keypad_deinit(keypad_instance_ctrl_t *const p_ctrl)
{
#if KEYPAD_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(NULL != p_ctrl->p_cfg, APP_ERR_INVALID_STATE);
#endif

    const keypad_config_t *const p_cfg = p_ctrl->p_cfg;

    /* Release all keypad lines before detaching the caller-owned configuration. */
    const app_err_t ret = p_cfg->p_port_api->p_write(p_cfg->p_port_context, p_cfg->released_pattern);

    /* The keypad owns no port resources, so reset its state even if the final port write fails. */
    p_ctrl->p_cfg         = NULL;
    p_ctrl->previous_keys = 0U;
    p_ctrl->stable_keys   = 0U;
    p_ctrl->init          = false;

    return ret;
}

/***********************************************************************************************************************
 * Scans the configured keypad matrix and returns its decoded key state.
 *
 * @param[in,out] p_ctrl   Pointer to an initialized runtime control block.
 * @param[out]    p_state  Pointer that receives the decoded keypad state.
 *
 * @retval APP_SUCCESS              The complete matrix was scanned and restored to its idle state.
 * @retval APP_ERR_INVALID_POINTER  A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE    The control block is not initialized.
 * @return                          An error returned by a configured port callback.
 **********************************************************************************************************************/
app_err_t keypad_scan(keypad_instance_ctrl_t *const p_ctrl, keypad_state_t *const p_state)
{
#if KEYPAD_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_state);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(NULL != p_ctrl->p_cfg, APP_ERR_INVALID_STATE);
#endif

    const keypad_config_t *const p_cfg        = p_ctrl->p_cfg;
    uint16_t                     pressed_keys = 0U;
    app_err_t                    ret          = APP_SUCCESS;

    /* Scan every row and accumulate only complete row results into the matrix bitmap. */
    for (uint8_t row = 0U; row < p_cfg->row_count; row++)
    {
        uint16_t row_keys = 0U;

        ret = keypad_scan_row(p_cfg, row, &row_keys);
        if (APP_SUCCESS != ret)
        {
            break;
        }

        pressed_keys |= row_keys;
    }

    /* Always restore the interrupt-ready idle pattern, including after a failed row operation. */
    const app_err_t restore_ret = p_cfg->p_port_api->p_write(p_cfg->p_port_context, p_cfg->idle_pattern);

    if (APP_SUCCESS != ret)
    {
        return ret;
    }
    if (APP_SUCCESS != restore_ret)
    {
        return restore_ret;
    }

    /* Debounce is not active yet, so a complete raw scan is treated as the current stable state. */
    p_ctrl->previous_keys   = p_ctrl->stable_keys;
    p_ctrl->stable_keys     = pressed_keys;
    p_state->pressed_keys   = pressed_keys;
    p_state->ghost_detected = keypad_is_ghost_detected(p_cfg, pressed_keys);

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

#if KEYPAD_CFG_PARAMETER_CHECKING
/***********************************************************************************************************************
 * Returns true only when mask selects exactly one port bit.
 *
 * A valid row or column mask must be non-zero and contain one set bit. Subtracting one clears that selected bit and
 * sets all lower bits. The bitwise AND is therefore zero only when the original mask contains one set bit.
 **********************************************************************************************************************/
static bool keypad_is_single_bit(uint8_t mask)
{
    return (mask != 0U) && ((mask & (uint8_t)(mask - 1U)) == 0U);
}
#endif

/***********************************************************************************************************************
 * Validates the control block, port interface and matrix configuration used by keypad_init().
 *
 * This function only validates caller-owned data; it does not access the keypad port. The configuration object and all
 * arrays referenced by it must remain valid and unchanged until keypad_deinit() is called.
 *
 * @param[in] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in] p_cfg   Pointer to the caller-owned keypad configuration.
 *
 * @retval APP_SUCCESS               All initialization arguments are valid.
 * @retval APP_ERR_INVALID_POINTER   A required API argument is NULL.
 * @retval APP_ERR_INVALID_STATE     The control block is already initialized.
 * @retval APP_ERR_INVALID_ARGUMENT  A required configuration member or matrix value is invalid.
 **********************************************************************************************************************/
static app_err_t keypad_validate_init_args(keypad_instance_ctrl_t *const p_ctrl, const keypad_config_t *const p_cfg)
{
#if KEYPAD_CFG_PARAMETER_CHECKING
    /* Check API arguments before dereferencing either caller-owned object. */
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);

    /* An active control block must be deinitialized before it can be configured again. */
    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);

    /* The port interface, matrix masks and keymap are mandatory configuration members. */
    APP_ERROR_RETURN(NULL != p_cfg->p_port_api, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_port_api->p_write, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_port_api->p_read, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_port_context, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_row_masks, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_column_masks, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(NULL != p_cfg->p_keymap, APP_ERR_INVALID_ARGUMENT);

    /* Bound loop indexes so the supported row and column dimensions cannot be exceeded. */
    APP_ERROR_RETURN((p_cfg->row_count > 0U) && (p_cfg->row_count <= KEYPAD_MAX_ROW_COUNT), APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((p_cfg->column_count > 0U) && (p_cfg->column_count <= KEYPAD_MAX_COLUMN_COUNT),
                     APP_ERR_INVALID_ARGUMENT);

    /* Each key consumes one bit in keypad_state_t::pressed_keys. */
    APP_ERROR_RETURN(((uint16_t)p_cfg->row_count * p_cfg->column_count) <= (sizeof(uint16_t) * CHAR_BIT),
                     APP_ERR_INVALID_ARGUMENT);

    uint8_t row_bits = 0U;

    /* Require one unique port bit per row and collect all row bits for the pattern checks below. */
    for (uint8_t row = 0U; row < p_cfg->row_count; row++)
    {
        const uint8_t row_mask = p_cfg->p_row_masks[row];

        APP_ERROR_RETURN(keypad_is_single_bit(row_mask), APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN(0U == (row_bits & row_mask), APP_ERR_INVALID_ARGUMENT);

        row_bits |= row_mask;
    }

    uint8_t column_bits = 0U;

    /* Require one unique port bit per column and collect all column bits for the pattern checks below. */
    for (uint8_t column = 0U; column < p_cfg->column_count; column++)
    {
        const uint8_t column_mask = p_cfg->p_column_masks[column];

        APP_ERROR_RETURN(keypad_is_single_bit(column_mask), APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN(0U == (column_bits & column_mask), APP_ERR_INVALID_ARGUMENT);

        column_bits |= column_mask;
    }

    /* A port pin cannot act as both a row output and a column input. */
    APP_ERROR_RETURN(0U == (row_bits & column_bits), APP_ERR_INVALID_ARGUMENT);

    const uint8_t used_bits = row_bits | column_bits;

    /* All keypad lines must be HIGH before the selected scan row is driven LOW. */
    APP_ERROR_RETURN(used_bits == (p_cfg->released_pattern & used_bits), APP_ERR_INVALID_ARGUMENT);

    /* Idle rows are LOW so any pressed key can pull its corresponding column LOW. */
    APP_ERROR_RETURN(0U == (p_cfg->idle_pattern & row_bits), APP_ERR_INVALID_ARGUMENT);

    /* Idle columns remain released HIGH so they can be sampled as active-LOW inputs. */
    APP_ERROR_RETURN(column_bits == (p_cfg->idle_pattern & column_bits), APP_ERR_INVALID_ARGUMENT);
#else
    (void)p_ctrl;
    (void)p_cfg;
#endif

    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Drives one matrix row LOW, samples every configured column and returns the pressed-key bits for that row.
 *
 * The helper starts from released_pattern, where all keypad lines are HIGH, then clears only the selected row bit. A
 * column sampled LOW represents a pressed key because the active row provides the path to LOW. The resulting key bits
 * use row-major order: bit index = (row_index * column_count) + column_index.
 *
 * This helper leaves the selected scan pattern latched. The caller must restore idle_pattern after the complete scan or
 * after an error from a later row.
 *
 * @param[in]  p_cfg       Pointer to the validated keypad configuration.
 * @param[in]  row_index   Zero-based index of the row to scan.
 * @param[out] p_row_keys  Pointer that receives the pressed-key bitmap for this row.
 *
 * @retval APP_SUCCESS  The row was written and all configured columns were sampled.
 * @return               An error returned by the configured port write or read callback.
 **********************************************************************************************************************/
static app_err_t keypad_scan_row(const keypad_config_t *const p_cfg, uint8_t row_index, uint16_t *const p_row_keys)
{
    /* Release every keypad line, then drive only the selected row LOW. */
    const uint8_t scan_pattern = p_cfg->released_pattern & (uint8_t)(~p_cfg->p_row_masks[row_index]);

    app_err_t ret = p_cfg->p_port_api->p_write(p_cfg->p_port_context, scan_pattern);
    if (APP_SUCCESS != ret)
    {
        return ret;
    }

    /* Initialize the sample to a released state so no key is implied before a successful port read. */
    uint8_t port_value = p_cfg->released_pattern;
    ret                = p_cfg->p_port_api->p_read(p_cfg->p_port_context, &port_value);
    if (APP_SUCCESS != ret)
    {
        return ret;
    }

    uint16_t row_keys = 0U;

    for (uint8_t column = 0U; column < p_cfg->column_count; column++)
    {
        const uint8_t column_mask = p_cfg->p_column_masks[column];

        if (0U == (port_value & column_mask))
        {
            const uint16_t key_index = ((uint16_t)row_index * p_cfg->column_count) + column;
            row_keys |= (uint16_t)(1U << key_index);
        }
    }

    /* Publish only a complete row result, leaving caller data unchanged if either port operation fails. */
    *p_row_keys = row_keys;
    return APP_SUCCESS;
}

/***********************************************************************************************************************
 * Detects an ambiguous rectangle in the scanned matrix.
 *
 * In a matrix without isolation diodes, two rows reporting at least two identical active columns can contain a phantom
 * key. The scan result remains available to the caller, while ghost_detected indicates that it must not be interpreted
 * as an unambiguous set of key presses.
 *
 * @param[in] p_cfg         Pointer to the validated keypad configuration.
 * @param[in] pressed_keys  Complete row-major pressed-key bitmap.
 *
 * @retval true   At least one ambiguous key rectangle was detected.
 * @retval false  No ambiguous key rectangle was detected.
 **********************************************************************************************************************/
static bool keypad_is_ghost_detected(const keypad_config_t *const p_cfg, uint16_t pressed_keys)
{
    const uint16_t column_mask = (uint16_t)((1U << p_cfg->column_count) - 1U);

    for (uint8_t first_row = 0U; first_row < p_cfg->row_count; first_row++)
    {
        const uint8_t  first_shift = first_row * p_cfg->column_count;
        const uint16_t first_keys  = (pressed_keys >> first_shift) & column_mask;

        for (uint8_t second_row = first_row + 1U; second_row < p_cfg->row_count; second_row++)
        {
            const uint8_t  second_shift = second_row * p_cfg->column_count;
            const uint16_t second_keys  = (pressed_keys >> second_shift) & column_mask;
            const uint16_t common_keys  = first_keys & second_keys;

            if ((common_keys != 0U) && ((common_keys & (uint16_t)(common_keys - 1U)) != 0U))
            {
                return true;
            }
        }
    }

    return false;
}
