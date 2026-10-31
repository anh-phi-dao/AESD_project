#ifndef FINGERPRINT_SENSOR_H_
#define FINGERPRINT_SENSOR_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /** Configure the UART and verify that the AS608 answers. */
    esp_err_t fingerprint_sensor_init(void);

    /** Read the number of templates stored in the module. */
    esp_err_t fingerprint_sensor_get_count(uint16_t *p_count);

    /** Enroll a finger (two scans) and store the template at page_id. Blocks until done or timeout. */
    esp_err_t fingerprint_sensor_enroll(uint16_t page_id);

    /** Scan a finger and search the library. ESP_ERR_NOT_FOUND when there is no match. */
    esp_err_t fingerprint_sensor_identify(uint16_t *p_page_id);

    /** One image, no waiting: ESP_OK and its features in buffer_id (1 or 2), ESP_ERR_NOT_FOUND when no finger is on
     * the sensor, ESP_FAIL when the image was too poor. buffer_id 0 only checks for a finger and keeps both buffers.
     * For callers that poll and must stay responsive. */
    esp_err_t fingerprint_sensor_capture(uint8_t buffer_id);

    /** Merge buffers 1 and 2 into a template and store it at page_id. ESP_FAIL when the two are not the same finger. */
    esp_err_t fingerprint_sensor_merge_store(uint16_t page_id);

    /** Search the library with buffer_id. ESP_ERR_NOT_FOUND when there is no match. */
    esp_err_t fingerprint_sensor_search(uint8_t buffer_id, uint16_t *p_page_id);

    /** Delete count templates starting from page_id. */
    esp_err_t fingerprint_sensor_delete(uint16_t page_id, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif /* FINGERPRINT_SENSOR_H_ */
