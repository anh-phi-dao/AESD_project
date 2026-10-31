#ifndef FINGERPRINT_TASK_H
#define FINGERPRINT_TASK_H

#include <stdint.h>

#define FINGERPRINT_TASK_TAG   "FINGER_TASK"
#define FINGERPRINT_TASK_STACK 4096U

/* Starts the AS608 and its task. Without a sensor it logs and returns: the lock keeps working with PIN and cards.
 * Call after boot_system_control(). */
void boot_fingerprint(void);

/* Deletes this user's template in the sensor. Only queues it: the fingerprint task is the only one on the UART.
 * Safe from any task; does nothing without a sensor. */
void fingerprint_task_request_delete(uint8_t user);

#endif
