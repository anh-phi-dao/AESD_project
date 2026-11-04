#ifndef TEMP_CODE_H_
#define TEMP_CODE_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Macro definitions
     ******************************************************************************************************************/

    /* Temporary guest codes, made on the web page and checked here, offline (docs/temp_code.md). Nothing about them
     * travels over MQTT: the lock and the page only share a secret. */
#define TEMP_CODE_DIGITS      8U
#define TEMP_CODE_VALID_MIN   30U /* A code opens the door for this many minutes after the minute it was made in. */
#define TEMP_CODE_AHEAD_MIN   2U  /* Codes up to this far in the future count too: the phone clock may run ahead. */
#define TEMP_CODE_SECRET_LEN  16U
#define TEMP_CODE_SECRET_HEX  (TEMP_CODE_SECRET_LEN * 2U) /* tmp_secret in devcfg: 32 hex characters. */
#define TEMP_CODE_TIME_MIN    1700000000 /* Unix time below this: the clock was never set (no SNTP since boot). */

    /*******************************************************************************************************************
     * Typedef definitions
     ******************************************************************************************************************/

    typedef enum e_temp_code_result
    {
        TEMP_CODE_WRONG = 0, /* Not a valid code now, or no secret was set. */
        TEMP_CODE_OK,
        TEMP_CODE_NO_TIME, /* The clock is not set: no code can be checked yet. */
    } temp_code_result_t;

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /** Set the shared secret from its 32 hex characters. "" or NULL turns temporary codes off. Safe from any task. */
    app_err_t temp_code_set_secret(const char *const p_hex);

    /** Whether p_code (TEMP_CODE_DIGITS digits) is valid at the current time. Quick: safe under a lock. */
    temp_code_result_t temp_code_check(const char *const p_code);

    /** The code of one minute slot (Unix time / 60) for a key, as a number below 10^TEMP_CODE_DIGITS. */
    app_err_t temp_code_at(const uint8_t *const p_key, size_t key_len, uint32_t slot, uint32_t *const p_code);

    /** Compares temp_code_at() with the vectors of scripts/temp_code.py. True when they match. */
    bool temp_code_self_test(void);

#ifdef __cplusplus
}
#endif

#endif /* TEMP_CODE_H_ */
