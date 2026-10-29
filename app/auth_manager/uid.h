#ifndef UID_H
#define UID_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "list.h"

#include "app_err.h"
#include "esp_log.h"
#include "keypad.h"

extern struct list_head uids_head;

enum
{
    INVALID_UID,
    VALID_UID,
};

/*Store UID information in RAM*/
typedef struct
{
    uint8_t          uid[8];
    uint8_t          uid_length;
    struct list_head list;
} uid_memory_storage_t;

uid_memory_storage_t *add_uid_to_list(struct list_head *uids_head, const uint8_t *uid, size_t uid_length);
void                  remove_last_uid_from_list(struct list_head *uids_head);
void                  remove_uid_from_list(struct list_head *uids_head, const uint8_t *uid, size_t uid_length);
void                  clear_uid_list(struct list_head *uids_head);
uint8_t               validate_uid(const uint8_t *uid, size_t uid_length, struct list_head *uids_head);
#endif
