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

    /** Delete count templates starting from page_id. */
    esp_err_t fingerprint_sensor_delete(uint16_t page_id, uint16_t count);

#ifdef __cplusplus
}
#endif

#endif /* FINGERPRINT_SENSOR_H_ */
