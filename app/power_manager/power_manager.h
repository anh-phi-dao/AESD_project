#ifndef POWER_MANAGER_H_
#define POWER_MANAGER_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "app_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Macro definitions
     ******************************************************************************************************************/

    /* Schematic rev3 (D6, to confirm): 3S1P 18650 2600 mAh + BMS 3S, 9.0-12.6 V. */
#define POWER_CAPACITY_MAH        2600
#define POWER_R_INT_MOHM          150  /* Pack internal resistance, for V + I*R_int. Measure on the real pack. */

#define POWER_SOC_LOW_PCT         20U  /* Thresholds with hysteresis: back to NORMAL only at POWER_SOC_RECOVER_PCT. */
#define POWER_SOC_CRITICAL_PCT    10U
#define POWER_SOC_RECOVER_PCT     25U
#define POWER_LOST_MV             9300 /* ~3.1 V/cell: the BMS cuts off soon. */

#define POWER_REST_MA             80   /* Below this the pack counts as resting, so its voltage is near OCV. */
#define POWER_REST_SYNC_MS        60000U
#define POWER_FULL_MV             12500 /* CV phase done: at or above this with almost no current, the pack is full. */
#define POWER_FULL_MA             50
#define POWER_AVG_SAMPLES         10U

    /*******************************************************************************************************************
     * Typedef definitions
     ******************************************************************************************************************/

    typedef enum e_power_level
    {
        POWER_LEVEL_UNKNOWN = 0, /* No reading yet, or the INA219 stopped answering. */
        POWER_LEVEL_NORMAL,
        POWER_LEVEL_LOW,
        POWER_LEVEL_CRITICAL,
    } power_level_t;

    typedef struct st_power_status
    {
        power_level_t level;
        uint8_t       soc_pct;    /* Averaged, 0..100. */
        int32_t       pack_mv;    /* Battery terminal voltage (bus + shunt drop). */
        int32_t       current_ma; /* Positive while discharging. */
        bool          power_lost; /* pack_mv under POWER_LOST_MV. */
    } power_status_t;

    typedef struct st_power_manager
    {
        int64_t        charge_uah; /* Remaining charge (coulomb counter), 0..capacity. */
        uint32_t       rest_ms;    /* How long the current stayed under POWER_REST_MA. */
        uint8_t        avg[POWER_AVG_SAMPLES];
        uint8_t        avg_count;
        uint8_t        avg_next;
        power_status_t status;
    } power_manager_t;

    /*******************************************************************************************************************
     * Public APIs
     ******************************************************************************************************************/

    /** Start from a saved charge (saved_uah >= 0), or from the OCV of the first reading when saved_uah < 0. */
    app_err_t power_manager_init(power_manager_t *const p_pm, int64_t saved_uah, int32_t pack_mv, int32_t current_ma);

    /** Add one reading taken dt_ms after the previous one. Updates p_pm->status. */
    app_err_t power_manager_update(power_manager_t *const p_pm, int32_t pack_mv, int32_t current_ma, uint32_t dt_ms);

    /** The INA219 stopped answering: level UNKNOWN until the next update. The charge is kept. */
    void power_manager_set_unknown(power_manager_t *const p_pm);

    /** SOC (0..100) of a resting 3S pack at this open-circuit voltage. */
    uint8_t power_manager_soc_from_ocv(int32_t ocv_mv);

#ifdef __cplusplus
}
#endif

#endif /* POWER_MANAGER_H_ */
