/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "fingerprint_sensor.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define TEST_TAG         "FP_TEST"
#define TEST_ENROLL_PAGE 1U

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Step 1: registers the reference finger. Repeats until the enrollment succeeds.
 **********************************************************************************************************************/
static void test_enroll_reference_finger(void)
{
    ESP_LOGI(TEST_TAG, "================ STEP 1: REGISTER FINGER ================");

    while (1)
    {
        ESP_LOGI(TEST_TAG, ">> Choose ONE finger to register. You will scan it twice.");

        esp_err_t ret = fingerprint_sensor_enroll(TEST_ENROLL_PAGE);
        if (ESP_OK == ret)
        {
            ESP_LOGI(TEST_TAG, "Register OK: finger saved at page %u", TEST_ENROLL_PAGE);
            return;
        }

        ESP_LOGW(TEST_TAG, "Register FAILED (%s). Try again in 2 seconds...", esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/***********************************************************************************************************************
 * Step 2: asks for a scan and reports whether it is the registered finger. Runs forever.
 **********************************************************************************************************************/
static void test_verify_loop(void)
{
    uint32_t attempt = 0U;

    ESP_LOGI(TEST_TAG, "================ STEP 2: VERIFY FINGER ================");

    while (1)
    {
        uint16_t page_id = 0U;

        attempt++;
        ESP_LOGI(TEST_TAG, ">> [Attempt %lu] Place a finger to verify...", (unsigned long)attempt);

        esp_err_t ret = fingerprint_sensor_identify(&page_id);
        if ((ESP_OK == ret) && (TEST_ENROLL_PAGE == page_id))
        {
            ESP_LOGI(TEST_TAG, "RESULT: CORRECT - this is the registered finger (page %u)", page_id);
        }
        else if (ESP_ERR_NOT_FOUND == ret)
        {
            ESP_LOGE(TEST_TAG, "RESULT: WRONG - this finger is NOT the registered finger");
        }
        else if (ESP_ERR_TIMEOUT == ret)
        {
            ESP_LOGW(TEST_TAG, "No finger detected, try again");
            continue;
        }
        else
        {
            ESP_LOGW(TEST_TAG, "Scan error (%s), try again", esp_err_to_name(ret));
        }

        ESP_LOGI(TEST_TAG, "Remove finger...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

void app_main(void)
{
    if (ESP_OK != fingerprint_sensor_init())
    {
        return;
    }

    test_enroll_reference_finger();
    vTaskDelay(pdMS_TO_TICKS(1500));
    test_verify_loop();
}
