#include "fingerprint_task.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "fingerprint_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "storage_task.h"
#include "system_control.h"

#define FINGERPRINT_POLL_MS 100U

/* Sensor page of a finger slot: page = slot + 1 (1..SYSTEM_FINGER_SLOTS). */

static TaskHandle_t  s_task         = NULL;
static QueueHandle_t s_delete_queue = NULL;  /* Pages to delete. Sent by any task, handled here. */
static bool          s_need_release = false; /* One action per touch: wait until the finger is lifted. */
static uint16_t      s_pending_page = 0U;    /* Stored after scan 2, not yet confirmed by scan 3. */

static void fingerprint_task(void *pvParameter);
static void fingerprint_identify(void);
static void fingerprint_enroll_step(const system_view_t *const p_view);
static void fingerprint_do_deletes(void);
static void fingerprint_drop_unknown(void);

void boot_fingerprint(void)
{
    if (ESP_OK != fingerprint_sensor_init())
    {
        ESP_LOGW(FINGERPRINT_TASK_TAG, "No fingerprint sensor, fingerprints disabled");
        return;
    }
    s_delete_queue = xQueueCreate(SYSTEM_FINGER_SLOTS, sizeof(uint8_t));
    if (NULL == s_delete_queue)
    {
        ESP_LOGE(FINGERPRINT_TASK_TAG, "No memory for the delete queue");
        return;
    }
    fingerprint_drop_unknown();
    if (pdPASS != xTaskCreatePinnedToCore(fingerprint_task, "fingerprint", FINGERPRINT_TASK_STACK, NULL, 1, &s_task, 1))
    {
        ESP_LOGE(FINGERPRINT_TASK_TAG, "Failed to create the fingerprint task");
    }
}

void fingerprint_task_request_delete(uint8_t page)
{
    if ((NULL == s_delete_queue) || (0U == page) || (page > SYSTEM_FINGER_SLOTS))
    {
        return;
    }
    if (pdTRUE != xQueueSend(s_delete_queue, &page, 0))
    {
        ESP_LOGW(FINGERPRINT_TASK_TAG, "Delete queue full, template of page %u kept", page);
    }
}

/***********************************************************************************************************************
 * Every FINGERPRINT_POLL_MS: on Home / Input / lockout a finger is identified, on ADD FINGERPRINT it is enrolled one
 * scan at a time, elsewhere the sensor is left alone. system_control decides what a match means.
 **********************************************************************************************************************/
static void fingerprint_task(void *pvParameter)
{
    (void)pvParameter;
    static system_view_t view; /* ~400 bytes: off the stack. */

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(FINGERPRINT_POLL_MS));
        fingerprint_do_deletes();
        if (!system_control_get_view(&view))
        {
            continue;
        }
        if ((0U != s_pending_page) && ((SYSTEM_STATE_ADMIN_SCAN != view.state) || (SYSTEM_CRED_FINGER != view.kind)))
        {
            /* ADD FINGERPRINT left before scan 3 (cancel, timeout): drop the half-made template, or the finger would
             * be found and counted as a wrong one. */
            (void)fingerprint_sensor_delete(s_pending_page, 1U);
            s_pending_page = 0U;
        }

        if (s_need_release)
        {
            /* GenImg also tells whether a finger is still there. */
            if (ESP_ERR_NOT_FOUND == fingerprint_sensor_capture(0U))
            {
                s_need_release = false;
            }
            continue;
        }

        switch (view.state)
        {
            case SYSTEM_STATE_HOME:
            case SYSTEM_STATE_PIN_ENTRY:
            case SYSTEM_STATE_LOCKOUT:
            case SYSTEM_STATE_ADMIN_LOCKOUT:
                fingerprint_identify();
                break;
            case SYSTEM_STATE_ADMIN_SCAN:
                if (SYSTEM_CRED_FINGER == view.kind)
                {
                    fingerprint_enroll_step(&view);
                }
                break;
            default:
                break;
        }
    }
}

static void fingerprint_identify(void)
{
    const esp_err_t ret = fingerprint_sensor_capture(1U);
    if (ESP_ERR_NOT_FOUND == ret)
    {
        return; /* No finger. */
    }
    s_need_release = true;
    if (ESP_OK != ret)
    {
        return; /* Poor image: not counted as a wrong finger, the user simply tries again. */
    }

    uint16_t page = 0U;
    if ((ESP_OK == fingerprint_sensor_search(1U, &page)) && (page >= 1U) && (page <= SYSTEM_FINGER_SLOTS))
    {
        system_control_finger((uint8_t)page); /* Unknown when that slot is free in system_control. */
    }
    else
    {
        system_control_finger(0U);
    }
}

/***********************************************************************************************************************
 * The three scans of ADD FINGERPRINT. The AS608 keeps two feature buffers:
 *   scan 1 -> buffer 1;  scan 2 -> buffer 2, merged and stored at the user's page (fails: not the same finger);
 *   scan 3 -> searched in the library, must find that page.
 * view.scan_count says which scan is next; system_control counts and saves on the third.
 **********************************************************************************************************************/
static void fingerprint_enroll_step(const system_view_t *const p_view)
{
    const uint8_t   scan = p_view->scan_count;
    const uint16_t  page = (uint16_t)(p_view->scan_slot + 1U);
    const esp_err_t ret  = fingerprint_sensor_capture((0U == scan) ? 1U : 2U);

    if (ESP_ERR_NOT_FOUND == ret)
    {
        return;
    }
    s_need_release = true;
    if (ESP_OK != ret)
    {
        system_control_finger_scanned(false);
        return;
    }

    switch (scan)
    {
        case 0U:
        {
            /* A finger already in the library would win every later search: refuse it, or drop a leftover. */
            uint16_t found = 0U;
            if (ESP_OK == fingerprint_sensor_search(1U, &found))
            {
                if (system_control_finger_scan_duplicate((uint8_t)found))
                {
                    break;
                }
                (void)fingerprint_sensor_delete(found, 1U);
            }
            system_control_finger_scanned(true);
            break;
        }

        case 1U:
            if (ESP_OK == fingerprint_sensor_merge_store(page))
            {
                s_pending_page = page;
                system_control_finger_scanned(true);
            }
            else
            {
                ESP_LOGW(FINGERPRINT_TASK_TAG, "Scans 1 and 2 are not the same finger");
                system_control_finger_scan_restart();
            }
            break;

        default:
        {
            uint16_t found = 0U;
            if ((ESP_OK == fingerprint_sensor_search(2U, &found)) && (found == page))
            {
                s_pending_page = 0U;
                system_control_finger_scanned(true); /* Third scan: system_control marks the finger. */
            }
            else
            {
                ESP_LOGW(FINGERPRINT_TASK_TAG, "Scan 3 does not match the stored template");
                (void)fingerprint_sensor_delete(page, 1U);
                s_pending_page = 0U;
                system_control_finger_scan_restart();
            }
            break;
        }
    }
}

/***********************************************************************************************************************
 * Deletes the templates of users who have no finger in system_control (what storage loaded, or nothing without an SD
 * card), or a finger left from before would match and count as a wrong one. Runs of free pages go in one command.
 **********************************************************************************************************************/
static void fingerprint_drop_unknown(void)
{
    static system_credentials_t s_cred; /* Static: too big for the boot stack. Only fingers are used, PINs wiped. */

    if (storage_is_unreadable())
    {
        /* system_control does not hold the real list: deleting to match it would lose every finger. */
        ESP_LOGW(FINGERPRINT_TASK_TAG, "Credentials not loaded, sensor templates kept");
        return;
    }

    bool ok = system_control_get_credentials(&s_cred, NULL);
    for (uint8_t first = 1U; ok && (first <= SYSTEM_FINGER_SLOTS);)
    {
        if (0U != s_cred.finger_user[first - 1U])
        {
            first++;
            continue;
        }
        uint8_t count = 1U;
        while (((first + count) <= SYSTEM_FINGER_SLOTS) && (0U == s_cred.finger_user[first + count - 1U]))
        {
            count++;
        }
        ok    = (ESP_OK == fingerprint_sensor_delete(first, count));
        first = (uint8_t)(first + count);
    }
    memset(&s_cred, 0, sizeof(s_cred));
    if (!ok)
    {
        ESP_LOGW(FINGERPRINT_TASK_TAG, "Could not clear the unused templates");
    }
}

static void fingerprint_do_deletes(void)
{
    uint8_t page = 0U;

    while (pdTRUE == xQueueReceive(s_delete_queue, &page, 0))
    {
        const esp_err_t ret = fingerprint_sensor_delete(page, 1U);
        ESP_LOGI(FINGERPRINT_TASK_TAG, "Template of page %u deleted: %s", page, esp_err_to_name(ret));
    }
}
