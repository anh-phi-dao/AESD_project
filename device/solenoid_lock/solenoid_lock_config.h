#ifndef SOLENOID_LOCK_CONFIG_H_
#define SOLENOID_LOCK_CONFIG_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "driver/gpio.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Schematic rev3: IO38 -> PC817 -> IRFZ44N -> 12 V solenoid, HIGH = unlocked (fail-secure).
 * Rev A has no external pull-down on IO38, so solenoid_lock_init() must run first in app_main. */
#define SOLENOID_LOCK_CTRL_PIN     GPIO_NUM_38
#define SOLENOID_LOCK_LED_PIN      GPIO_NUM_4 /* Status LED, ON while unlocked. */

#define SOLENOID_LOCK_ACTIVE_LEVEL 1U

/* Hard limit on one unlock, enforced inside the driver whatever the application does. It must stay longer than
 * SYSTEM_UNLOCK_MS (5 s) so it only trips when the normal relock is missed. */
#define SOLENOID_LOCK_GUARD_MS     6000U

#endif /* SOLENOID_LOCK_CONFIG_H_ */
