#include "fingerprint_task.h"

#include <stdbool.h>

#include "esp_log.h"
#include "fingerprint_sensor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_control.h"

#define FINGERPRINT_POLL_MS 100U

/* Sensor page of a user: page = user ID (1..SYSTEM_USER_COUNT). */

static TaskHandle_t      s_task         = NULL;
static volatile uint32_t s_delete_mask  = 0U;    /* Bit n: delete page n. Set by any task, cleared here. */
static bool              s_need_release = false; /* One action per touch: wait until the finger is lifted. */
static uint16_t          s_pending_page = 0U;    /* Stored after scan 2, not yet confirmed by scan 3. */

static void fingerprint_task(void *pvParameter);
static void fingerprint_identify(void);
static void fingerprint_enroll_step(const system_view_t *const p_view);
static void fingerprint_do_deletes(void);

void boot_fingerprint(void)
{
    if (ESP_OK != fingerprint_sensor_init())
    {
        ESP_LOGW(FINGERPRINT_TASK_TAG, "No fingerprint sensor, fingerprints disabled");
        return;
    }
    /* system_control keeps which users have a finger in RAM only: start the sensor empty too, or a finger enrolled
     * before the restart would match and count as a wrong one. TODO(storage): drop this once both are kept in flash. */
    if (ESP_OK != fingerprint_sensor_delete(1U, SYSTEM_USER_COUNT))
    {
        ESP_LOGW(FINGERPRINT_TASK_TAG, "Could not clear the user templates");
    }
    if (pdPASS != xTaskCreatePinnedToCore(fingerprint_task, "fingerprint", FINGERPRINT_TASK_STACK, NULL, 1, &s_task, 1))
    {
        ESP_LOGE(FINGERPRINT_TASK_TAG, "Failed to create the fingerprint task");
    }
}

void fingerprint_task_request_delete(uint8_t user)
{
    if ((NULL == s_task) || (0U == user) || (user > SYSTEM_USER_COUNT))
    {
        return;
    }
    __atomic_fetch_or(&s_delete_mask, 1UL << user, __ATOMIC_RELAXED);
}

/***********************************************************************************************************************
 * Every FINGERPRINT_POLL_MS: on Home / Input / lockout a finger is identified, on ADD FINGERPRINT it is enrolled one
 * scan at a time, elsewhere the sensor is left alone. system_control decides what a match means.
 **********************************************************************************************************************/
static void fingerprint_task(void *pvParameter)
{
    (void)pvParameter;
    system_view_t view;

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
    if ((ESP_OK == fingerprint_sensor_search(1U, &page)) && (page >= 1U) && (page <= SYSTEM_USER_COUNT))
    {
        system_control_finger((uint8_t)page); /* Unknown when that ID has no finger in system_control. */
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
    const uint16_t  page = p_view->scan_user;
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
            system_control_finger_scanned(true);
            break;

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

static void fingerprint_do_deletes(void)
{
    const uint32_t mask = __atomic_exchange_n(&s_delete_mask, 0U, __ATOMIC_RELAXED);

    for (uint8_t user = 1U; user <= SYSTEM_USER_COUNT; user++)
    {
        if (0U != (mask & (1UL << user)))
        {
            const esp_err_t ret = fingerprint_sensor_delete(user, 1U);
            ESP_LOGI(FINGERPRINT_TASK_TAG, "Template of user %u deleted: %s", user, esp_err_to_name(ret));
        }
    }
}
