/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "power_manager.h"

#include <stddef.h>
#include <string.h>

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define POWER_CAPACITY_UAH ((int64_t)POWER_CAPACITY_MAH * 1000)
#define POWER_OCV_POINTS   (sizeof(s_ocv_table) / sizeof(s_ocv_table[0]))

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

typedef struct st_power_ocv_point
{
    int32_t mv; /* Whole pack (3 cells). */
    uint8_t soc;
} power_ocv_point_t;

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static uint8_t       power_soc_from_charge(int64_t charge_uah);
static int64_t       power_charge_from_soc(uint8_t soc);
static uint8_t       power_average(power_manager_t *const p_pm, uint8_t soc);
static power_level_t power_next_level(power_level_t level, uint8_t soc);

/***********************************************************************************************************************
 * Private global variables
 **********************************************************************************************************************/

/* Schematic rev3 table C: ~3 x OCV of an 18650 cell at rest, 25 C. Recalibrate with the real pack. */
static const power_ocv_point_t s_ocv_table[] = {
    {9600, 0},   {10200, 5},  {10800, 10}, {10950, 20}, {11070, 30}, {11190, 40},
    {11310, 50}, {11460, 60}, {11700, 70}, {12000, 80}, {12300, 90}, {12600, 100},
};

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

app_err_t power_manager_init(power_manager_t *const p_pm, int64_t saved_uah, int32_t pack_mv, int32_t current_ma)
{
    APP_ASSERT(NULL != p_pm);

    memset(p_pm, 0, sizeof(*p_pm));
    if ((saved_uah >= 0) && (saved_uah <= POWER_CAPACITY_UAH))
    {
        p_pm->charge_uah = saved_uah;
    }
    else
    {
        /* Under load the terminal voltage sags by I * R_int: add it back to approach the OCV. */
        const int32_t ocv_mv = pack_mv + ((current_ma * POWER_R_INT_MOHM) / 1000);
        p_pm->charge_uah     = power_charge_from_soc(power_manager_soc_from_ocv(ocv_mv));
    }
    p_pm->status.level = POWER_LEVEL_UNKNOWN;

    return power_manager_update(p_pm, pack_mv, current_ma, 0U);
}

app_err_t power_manager_update(power_manager_t *const p_pm, int32_t pack_mv, int32_t current_ma, uint32_t dt_ms)
{
    APP_ASSERT(NULL != p_pm);

    /* Coulomb counting: mA * ms / 1000 = uAh * 3.6, so divide by 3600 for uAh. */
    p_pm->charge_uah -= ((int64_t)current_ma * (int64_t)dt_ms) / 3600;

    const int32_t abs_ma = (current_ma < 0) ? -current_ma : current_ma;
    p_pm->rest_ms        = (abs_ma < POWER_REST_MA) ? (p_pm->rest_ms + dt_ms) : 0U;

    if ((pack_mv >= POWER_FULL_MV) && (abs_ma < POWER_FULL_MA))
    {
        p_pm->charge_uah = POWER_CAPACITY_UAH;
    }
    else if (p_pm->rest_ms >= POWER_REST_SYNC_MS)
    {
        /* Resting long enough: pull the counter halfway to the OCV estimate, so its drift is corrected without a
         * jump from one noisy reading. */
        const int32_t ocv_mv = pack_mv + ((current_ma * POWER_R_INT_MOHM) / 1000);
        const int64_t ocv_uah = power_charge_from_soc(power_manager_soc_from_ocv(ocv_mv));
        p_pm->charge_uah      = (p_pm->charge_uah + ocv_uah) / 2;
        p_pm->rest_ms         = 0U;
    }

    if (p_pm->charge_uah < 0)
    {
        p_pm->charge_uah = 0;
    }
    else if (p_pm->charge_uah > POWER_CAPACITY_UAH)
    {
        p_pm->charge_uah = POWER_CAPACITY_UAH;
    }

    const uint8_t soc = power_average(p_pm, power_soc_from_charge(p_pm->charge_uah));

    p_pm->status.soc_pct    = soc;
    p_pm->status.pack_mv    = pack_mv;
    p_pm->status.current_ma = current_ma;
    p_pm->status.power_lost = (pack_mv < POWER_LOST_MV);
    p_pm->status.level      = power_next_level(p_pm->status.level, soc);
    return APP_SUCCESS;
}

void power_manager_set_unknown(power_manager_t *const p_pm)
{
    if (NULL != p_pm)
    {
        p_pm->status.level = POWER_LEVEL_UNKNOWN;
    }
}

uint8_t power_manager_soc_from_ocv(int32_t ocv_mv)
{
    if (ocv_mv <= s_ocv_table[0].mv)
    {
        return 0U;
    }
    for (size_t i = 1U; i < POWER_OCV_POINTS; i++)
    {
        if (ocv_mv <= s_ocv_table[i].mv)
        {
            const power_ocv_point_t *const p_lo = &s_ocv_table[i - 1U];
            const power_ocv_point_t *const p_hi = &s_ocv_table[i];
            const int32_t span = (int32_t)(p_hi->soc - p_lo->soc);
            return (uint8_t)(p_lo->soc + ((ocv_mv - p_lo->mv) * span) / (p_hi->mv - p_lo->mv));
        }
    }
    return 100U;
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static uint8_t power_soc_from_charge(int64_t charge_uah)
{
    return (uint8_t)((charge_uah * 100) / POWER_CAPACITY_UAH);
}

static int64_t power_charge_from_soc(uint8_t soc)
{
    return (POWER_CAPACITY_UAH * soc) / 100;
}

static uint8_t power_average(power_manager_t *const p_pm, uint8_t soc)
{
    p_pm->avg[p_pm->avg_next] = soc;
    p_pm->avg_next            = (uint8_t)((p_pm->avg_next + 1U) % POWER_AVG_SAMPLES);
    if (p_pm->avg_count < POWER_AVG_SAMPLES)
    {
        p_pm->avg_count++;
    }

    uint32_t sum = 0U;
    for (uint8_t i = 0U; i < p_pm->avg_count; i++)
    {
        sum += p_pm->avg[i];
    }
    return (uint8_t)(sum / p_pm->avg_count);
}

static power_level_t power_next_level(power_level_t level, uint8_t soc)
{
    if (soc <= POWER_SOC_CRITICAL_PCT)
    {
        return POWER_LEVEL_CRITICAL;
    }
    if (soc <= POWER_SOC_LOW_PCT)
    {
        /* From CRITICAL, stay there until the recover threshold, like LOW -> NORMAL. */
        return (POWER_LEVEL_CRITICAL == level) ? POWER_LEVEL_CRITICAL : POWER_LEVEL_LOW;
    }
    if ((soc < POWER_SOC_RECOVER_PCT) && ((POWER_LEVEL_LOW == level) || (POWER_LEVEL_CRITICAL == level)))
    {
        return level;
    }
    return POWER_LEVEL_NORMAL;
}
