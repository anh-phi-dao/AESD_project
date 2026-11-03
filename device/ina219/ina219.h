#ifndef INA219_H_
#define INA219_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdint.h>

#include "app_err.h"
#include "driver/i2c_master.h"
#include "ina219_config.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Typedef definitions
     ******************************************************************************************************************/

    typedef struct st_ina219_reading
    {
        int32_t bus_mv;     /* Voltage at IN- (load side of the shunt), mV. */
        int32_t current_ma; /* Through the shunt, IN+ -> IN- positive (the pack discharging), mA. */
        int32_t power_mw;
    } ina219_reading_t;

    typedef struct st_ina219_instance_ctrl
    {
        i2c_master_dev_handle_t dev;
    } ina219_instance_ctrl_t;

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /** Add the INA219 to an existing bus, write the configuration and the calibration, and check it answers. */
    app_err_t ina219_open(ina219_instance_ctrl_t *const p_ctrl, i2c_master_bus_handle_t bus);

    /** Write the configuration and the calibration again, e.g. after the module lost power. */
    app_err_t ina219_configure(ina219_instance_ctrl_t *const p_ctrl);

    /** One reading. APP_ERR_INVALID_STATE when the module lost its calibration (call ina219_configure()). */
    app_err_t ina219_read(ina219_instance_ctrl_t *const p_ctrl, ina219_reading_t *const p_out);

    /** Remove the INA219 from the bus. The bus itself stays. */
    void ina219_close(ina219_instance_ctrl_t *const p_ctrl);

#ifdef __cplusplus
}
#endif

#endif /* INA219_H_ */
