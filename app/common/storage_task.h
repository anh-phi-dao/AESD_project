#ifndef STORAGE_TASK_H
#define STORAGE_TASK_H

#include <stdbool.h>

#define STORAGE_TASK_TAG      "STORAGE_TASK"
#define STORAGE_TASK_STACK    4096U
#define STORAGE_TASK_POLL_MS  500U  /* How often system_control is checked for a credential change. */
#define STORAGE_TASK_RETRY_MS 5000U /* After a failed save. */

/* Mounts the SD card and loads the PINs, cards and fingers of every user into system_control. Without a card, or when
 * the file cannot be read, it logs and returns: the lock keeps working from RAM with the DEV DEFAULTS. Call after
 * boot_system_control() and before the fingerprint sensor, the card reader and the keypad. */
void boot_storage(void);

/* True when a credentials file is on the card but could not be read: what system_control holds is not the truth,
 * so nothing may be deleted to match it (e.g. sensor templates). */
bool storage_is_unreadable(void);

/* Writes the credentials to the SD card each time the admin menu changes them. Returns at once when boot_storage()
 * could not load them, so a file that was there but unreadable is never overwritten. */
void storage_task(void *pvParameter);

#endif
