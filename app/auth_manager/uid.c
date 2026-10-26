#include "uid.h"

struct list_head uids_head = LIST_HEAD_INIT(uids_head);

uid_memory_storage_t *add_uid_to_list(struct list_head *uids_head, const uint8_t *uid, size_t uid_length)
{
    if (uid_length > 8)
    {
        ESP_LOGE("UID", "UID length exceeds maximum allowed length of 8 bytes");
        return NULL;
    }

    if (list_empty(uids_head))
    {
        ESP_LOGI("UID", "UID list is empty, adding first UID");
    }
    else
    {
        uid_memory_storage_t *current_uid;
        list_for_each_entry(current_uid, uids_head, list)
        {
            if (memcmp(uid, current_uid->uid, uid_length) == 0)
            {
                ESP_LOGI("UID", "UID already exists in the list");
                return current_uid;
            }
        }
    }

    uid_memory_storage_t *new_uid = (uid_memory_storage_t *)malloc(sizeof(uid_memory_storage_t));
    if (!new_uid)
    {
        ESP_LOGE("UID", "Failed to allocate memory for new UID");
        return NULL;
    }

    memcpy(new_uid->uid, uid, uid_length);
    new_uid->uid_length = uid_length;
    INIT_LIST_HEAD(&new_uid->list);
    list_add_tail(&new_uid->list, uids_head);

    return new_uid;
}

void remove_last_uid_from_list(struct list_head *uids_head)
{
    if (list_empty(uids_head))
    {
        ESP_LOGI("UID", "[%s]UID list is empty, nothing to remove", __FUNCTION__);
        return;
    }

    uid_memory_storage_t *last_uid = list_last_entry(uids_head, uid_memory_storage_t, list);
    list_del(&last_uid->list);
    free(last_uid);
}

void remove_uid_from_list(struct list_head *uids_head, const uint8_t *uid, size_t uid_length)
{
    uid_memory_storage_t *current_uid;
    if (list_empty(uids_head))
    {
        ESP_LOGI("UID", "[%s]UID list is empty, nothing to remove", __FUNCTION__);
        return;
    }

    list_for_each_entry(current_uid, uids_head, list)
    {
        if (memcmp(uid, current_uid->uid, uid_length) == 0)
        {
            list_del(&current_uid->list);
            free(current_uid);
            return;
        }
    }
}

void clear_uid_list(struct list_head *uids_head)
{
    uid_memory_storage_t *current_uid, *tmp;
    if (list_empty(uids_head))
    {
        ESP_LOGI("UID", "[%s]UID list is empty, nothing to remove", __FUNCTION__);
        return;
    }
    list_for_each_entry_safe(current_uid, tmp, uids_head, list)
    {
        list_del(&current_uid->list);
        free(current_uid);
    }
}

uint8_t validate_uid(const uint8_t *uid, size_t uid_length, struct list_head *uids_head)
{
    uid_memory_storage_t *valid_uids;
    if (list_empty(uids_head))
    {
        ESP_LOGI("UID", "[%s]UID list is empty, result is always INVALID_UID", __FUNCTION__);
        return INVALID_UID;
    }
    list_for_each_entry(valid_uids, uids_head, list)
    {
        if (memcmp(uid, valid_uids->uid, uid_length) == 0)
        {
            return VALID_UID;
        }
    }
    return INVALID_UID;
}
