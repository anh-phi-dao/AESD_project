/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "ina219.h"

#include <stddef.h>

#include "esp_log.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

#define INA219_TAG          "INA219"

#define INA219_REG_CONFIG   0x00U
#define INA219_REG_SHUNT    0x01U
#define INA219_REG_BUS      0x02U
#define INA219_REG_POWER    0x03U
#define INA219_REG_CURRENT  0x04U
#define INA219_REG_CALIB    0x05U

#define INA219_CONFIG_RESET 0x8000U

#define INA219_BUS_OVF      0x0001U /* Math overflow: current or power out of range. */
#define INA219_BUS_SHIFT    3U
#define INA219_BUS_LSB_MV   4

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static app_err_t ina219_write_reg(ina219_instance_ctrl_t *const p_ctrl, uint8_t reg, uint16_t value);
static app_err_t ina219_read_reg(ina219_instance_ctrl_t *const p_ctrl, uint8_t reg, uint16_t *const p_value);

/***********************************************************************************************************************
 * Public functions
 **********************************************************************************************************************/

app_err_t ina219_open(ina219_instance_ctrl_t *const p_ctrl, i2c_master_bus_handle_t bus)
{
    APP_ASSERT((NULL != p_ctrl) && (NULL != bus));

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = INA219_I2C_ADDRESS,
        .scl_speed_hz    = INA219_I2C_SPEED_HZ,
    };
    if (ESP_OK != i2c_master_bus_add_device(bus, &dev_cfg, &p_ctrl->dev))
    {
        p_ctrl->dev = NULL;
        return APP_FAIL;
    }

    /* Reset first so a module that kept power across an ESP reset starts from known registers. */
    app_err_t ret = ina219_write_reg(p_ctrl, INA219_REG_CONFIG, INA219_CONFIG_RESET);
    if (APP_SUCCESS == ret)
    {
        ret = ina219_configure(p_ctrl);
    }
    if (APP_SUCCESS != ret)
    {
        ESP_LOGE(INA219_TAG, "No answer at 0x%02X", INA219_I2C_ADDRESS);
        ina219_close(p_ctrl);
    }
    return ret;
}

app_err_t ina219_configure(ina219_instance_ctrl_t *const p_ctrl)
{
    APP_ASSERT((NULL != p_ctrl) && (NULL != p_ctrl->dev));

    app_err_t ret = ina219_write_reg(p_ctrl, INA219_REG_CONFIG, INA219_CONFIG_VALUE);
    if (APP_SUCCESS == ret)
    {
        ret = ina219_write_reg(p_ctrl, INA219_REG_CALIB, INA219_CALIBRATION);
    }

    uint16_t readback = 0U;
    if (APP_SUCCESS == ret)
    {
        ret = ina219_read_reg(p_ctrl, INA219_REG_CONFIG, &readback);
    }
    if ((APP_SUCCESS == ret) && (INA219_CONFIG_VALUE != readback))
    {
        ESP_LOGE(INA219_TAG, "Config readback 0x%04X, expected 0x%04X", readback, INA219_CONFIG_VALUE);
        ret = APP_FAIL;
    }
    return ret;
}

app_err_t ina219_read(ina219_instance_ctrl_t *const p_ctrl, ina219_reading_t *const p_out)
{
    APP_ASSERT((NULL != p_ctrl) && (NULL != p_ctrl->dev) && (NULL != p_out));

    uint16_t calib   = 0U;
    uint16_t bus     = 0U;
    uint16_t current = 0U;
    uint16_t power   = 0U;

    app_err_t ret = ina219_read_reg(p_ctrl, INA219_REG_CALIB, &calib);
    if (APP_SUCCESS == ret)
    {
        ret = ina219_read_reg(p_ctrl, INA219_REG_BUS, &bus);
    }
    if (APP_SUCCESS == ret)
    {
        ret = ina219_read_reg(p_ctrl, INA219_REG_CURRENT, &current);
    }
    if (APP_SUCCESS == ret)
    {
        ret = ina219_read_reg(p_ctrl, INA219_REG_POWER, &power);
    }
    APP_ERROR_RETURN(APP_SUCCESS == ret, ret);

    /* A power glitch on the module clears CAL, and current/power then read 0 although the bus voltage is fine. */
    APP_ERROR_RETURN(INA219_CALIBRATION == calib, APP_ERR_INVALID_STATE);
    if (0U != (bus & INA219_BUS_OVF))
    {
        ESP_LOGW(INA219_TAG, "Overflow: current above the 3.2 A range");
    }

    p_out->bus_mv     = (int32_t)(bus >> INA219_BUS_SHIFT) * INA219_BUS_LSB_MV;
    p_out->current_ma = ((int32_t)(int16_t)current * INA219_CURRENT_LSB_UA) / 1000;
    p_out->power_mw   = ((int32_t)power * 20 * INA219_CURRENT_LSB_UA) / 1000;
    return APP_SUCCESS;
}

void ina219_close(ina219_instance_ctrl_t *const p_ctrl)
{
    if ((NULL != p_ctrl) && (NULL != p_ctrl->dev))
    {
        (void)i2c_master_bus_rm_device(p_ctrl->dev);
        p_ctrl->dev = NULL;
    }
}

/***********************************************************************************************************************
 * Private functions
 **********************************************************************************************************************/

static app_err_t ina219_write_reg(ina219_instance_ctrl_t *const p_ctrl, uint8_t reg, uint16_t value)
{
    const uint8_t buf[3] = {reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFFU)};

    /* The i2c_master driver serializes transfers on the bus, so the PN532 and the PCF8574 are never interleaved. */
    const esp_err_t err = i2c_master_transmit(p_ctrl->dev, buf, sizeof(buf), INA219_I2C_TIMEOUT_MS);
    return (ESP_OK == err) ? APP_SUCCESS : APP_ERR_TIMEOUT;
}

static app_err_t ina219_read_reg(ina219_instance_ctrl_t *const p_ctrl, uint8_t reg, uint16_t *const p_value)
{
    uint8_t rx[2] = {0U, 0U};

    const esp_err_t err = i2c_master_transmit_receive(p_ctrl->dev, &reg, 1U, rx, sizeof(rx), INA219_I2C_TIMEOUT_MS);
    APP_ERROR_RETURN(ESP_OK == err, APP_ERR_TIMEOUT);

    *p_value = (uint16_t)(((uint16_t)rx[0] << 8) | rx[1]);
    return APP_SUCCESS;
}
