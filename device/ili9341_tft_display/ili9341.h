#ifndef ILI9341_H_
#define ILI9341_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdio.h>
#include "ili9341_config.h"
#include "lvgl.h"
#include "spi.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Macro definitions
     ******************************************************************************************************************/

    /*******************************************************************************************************************
     * Typedef definitions
     ******************************************************************************************************************/

    /* Runtime configuration handle for the ILI9341 display driver. */
    typedef struct
    {
        gpio_num_t          e_dc_pin;     /* Data/Command selection GPIO pin. */
        gpio_num_t          e_rst_pin;    /* Hardware reset GPIO pin. */
        spi_device_handle_t *p_spi_Handle; /* Pointer to the SPI device handle. */
    } ili9341_handle_t;

    /*******************************************************************************************************************
     * Exported global variables
     ******************************************************************************************************************/

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /* Reset the display, send the initialization command sequence, and configure orientation. */
    void DEV_ILI9341_Init(ili9341_handle_t *p_ili9341_handle);

    /* Flush a rectangular region of pixel colors to the ILI9341 display memory. */
    void DEV_ILI9341_Flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map);

    /* Enable or disable the display backlight. */
    void DEV_ILI9341_EnableBacklight(bool backlight);

    /* Put the ILI9341 display controller into sleep mode. */
    void DEV_ILI9341_SleepIn(void);

    /* Wake the ILI9341 display controller from sleep mode. */
    void DEV_ILI9341_SleepOut(void);

#ifdef __cplusplus
}
#endif

#endif /* ILI9341_H_ */