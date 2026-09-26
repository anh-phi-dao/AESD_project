#ifndef ERR_MAP_H_
#define ERR_MAP_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "app_err.h"
#include "esp_err.h"

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

    /* Convert an ESP-IDF error code to the application error contract. */
    app_err_t err_map_esp_to_app(esp_err_t esp_error);

#ifdef __cplusplus
}
#endif

#endif /* ERR_MAP_H_ */
