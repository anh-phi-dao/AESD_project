#ifndef SOLENOID_LOCK_H_
#define SOLENOID_LOCK_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>

#include "app_err.h"
#include "solenoid_lock_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /** Drive the solenoid LOW (locked) and create the guard timer. Call it first in app_main. Safe to call again. */
    app_err_t solenoid_lock_init(void);

    /** Unlock: solenoid and LED ON, (re)start the guard. APP_ERR_INVALID_STATE before init, then nothing is driven. */
    app_err_t solenoid_lock_unlock(void);

    /** Lock: solenoid and LED OFF, stop the guard. Unconditional, works even before init. */
    void solenoid_lock_lock(void);

    /** true while the solenoid is driven ON. */
    bool solenoid_lock_is_unlocked(void);

    /** true when the guard had to relock the door since the last call (the application missed its relock). Clears. */
    bool solenoid_lock_take_guard_tripped(void);

#ifdef __cplusplus
}
#endif

#endif /* SOLENOID_LOCK_H_ */
