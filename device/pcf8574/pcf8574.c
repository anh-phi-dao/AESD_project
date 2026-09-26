/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "pcf8574.h"
#include "err_map.h"
#include "esp_log.h"
#include "soc/soc_caps.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************B************************************/

#define PCF8574_TAG               "PCF8574"
#define PCF8574_I2C_TIMEOUT_MS    1000
#define PCF8574_SCAN_FIRST_ADDR   0x08U
#define PCF8574_SCAN_END_ADDR     0x78U
#define PCF8574_SCAN_TIMEOUT_MS   50

#if USED_FREERTOS
#define PCF8574_INTR_STACK_SIZE      3072
#define PCF8574_INTR_TASK_PRIORITY   5
#endif

/***********************************************************************************************************************
 * Typedef definitions
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Private function prototypes
 **********************************************************************************************************************/

static esp_err_t pcf8574_release(pcf8574_instance_ctrl_t *const p_ctrl);
static app_err_t pcf8574_validate_init_args(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg);
static esp_err_t pcf8574_setup_i2c_bus(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg);
static esp_err_t pcf8574_setup_i2c_device(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg);
#if USED_FREERTOS
static app_err_t pcf8574_create_i2c_transfer_mutex(pcf8574_instance_ctrl_t *const p_ctrl);
static esp_err_t pcf8574_setup_interrupt(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg);
static void      pcf8574_gpio_isr_handler(void *p_api_ctrl);
static void      pcf8574_intr_task(void *p_api_ctrl);
#endif

/***********************************************************************************************************************
 * Public APIs
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Initializes internal driver data and the configured I2C device.
 *
 * @param[in,out] p_ctrl  Pointer to the caller-owned runtime control block.
 * @param[in]     p_cfg   Configuration that remains valid and unchanged until deinit.
 *
 * @retval APP_SUCCESS                  Initialization succeeded.
 * @retval APP_ERR_INVALID_ARGUMENT     A configuration value is invalid.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block is already initialized.
 * @retval APP_ERR_NOT_SUPPORTED        INT processing was requested without FreeRTOS.
 * @retval APP_ERR_NO_MEMORY            A required driver resource could not be allocated.
 * @return                              A mapped hardware or system error can also be returned.
 **********************************************************************************************************************/
app_err_t pcf8574_init(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg)
{
    app_err_t app_ret = pcf8574_validate_init_args(p_ctrl, p_cfg);
    if (app_ret != APP_SUCCESS)
    {
        return app_ret;
    }

    p_ctrl->p_cfg = p_cfg;

    esp_err_t ret         = ESP_OK;
    esp_err_t cleanup_ret = ESP_OK;

#if USED_FREERTOS
    app_ret = pcf8574_create_i2c_transfer_mutex(p_ctrl);
    if (app_ret != APP_SUCCESS)
    {
        p_ctrl->p_cfg = NULL;
        return app_ret;
    }
#endif

    ret = pcf8574_setup_i2c_bus(p_ctrl, p_cfg);
    if (ret != ESP_OK)
    {
        goto fail;
    }

    ret = pcf8574_setup_i2c_device(p_ctrl, p_cfg);
    if (ret != ESP_OK)
    {
        goto fail;
    }

#if USED_FREERTOS
    ret = pcf8574_setup_interrupt(p_ctrl, p_cfg);
    if (ret != ESP_OK)
    {
        goto fail;
    }
#endif

    p_ctrl->init = true;
    ESP_LOGI(PCF8574_TAG, "Initialized I2C address 0x%02X", p_cfg->i2c_device_config.device_address);
    return APP_SUCCESS;

fail:
    cleanup_ret = pcf8574_release(p_ctrl);
    if (cleanup_ret != ESP_OK)
    {
        ESP_LOGE(PCF8574_TAG, "Initialization cleanup failed: %s", esp_err_to_name(cleanup_ret));
    }
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * De-initializes driver data and releases resources owned by this instance. A borrowed I2C bus remains owned by the
 * caller.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval APP_SUCCESS                  De-initialization succeeded.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @return                              A mapped hardware or system error can also be returned.
 **********************************************************************************************************************/
app_err_t pcf8574_deinit(pcf8574_instance_ctrl_t *const p_ctrl)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

    return err_map_esp_to_app(pcf8574_release(p_ctrl));
}

/***********************************************************************************************************************
 * Writes all eight output latch bits P0..P7 in one I2C transaction.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 * @param[in]     data    A 1 releases a pin; a 0 drives it LOW.
 *
 * @retval APP_SUCCESS                  Port write succeeded.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @retval APP_ERR_TIMEOUT              The I2C mutex could not be acquired in a FreeRTOS build.
 * @return                              A mapped hardware or system error can also be returned.
 **********************************************************************************************************************/
app_err_t pcf8574_write(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t data)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(NULL != p_ctrl->p_i2c_dev, APP_ERR_INVALID_STATE);
#endif

    /* Serialize I2C access when FreeRTOS synchronization is enabled. */
#if USED_FREERTOS
    if (xSemaphoreTake(p_ctrl->p_i2c_transfer_mutex, pdMS_TO_TICKS(PCF8574_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    esp_err_t ret = i2c_master_transmit(p_ctrl->p_i2c_dev, &data, sizeof(data), PCF8574_I2C_TIMEOUT_MS);
    /* Release the transfer lock after the I2C operation is complete. */
#if USED_FREERTOS
    xSemaphoreGive(p_ctrl->p_i2c_transfer_mutex);
#endif
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Reads physical levels P0..P7 in one I2C transaction and acknowledges the device interrupt.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 * @param[out]    p_data  Pointer that receives the port value.
 *
 * @retval APP_SUCCESS                  Port read succeeded.
 * @retval APP_ERR_INVALID_POINTER      A required pointer is NULL.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @retval APP_ERR_TIMEOUT              The I2C mutex could not be acquired in a FreeRTOS build.
 * @return                              A mapped hardware or system error can also be returned.
 **********************************************************************************************************************/
app_err_t pcf8574_read(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t *const p_data)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_data);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(NULL != p_ctrl->p_i2c_dev, APP_ERR_INVALID_STATE);
#endif

    /* Serialize I2C transfers when FreeRTOS synchronization is enabled. */ // Sai ne
#if USED_FREERTOS
    if (xSemaphoreTake(p_ctrl->p_i2c_transfer_mutex, pdMS_TO_TICKS(PCF8574_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    uint8_t   value = PCF8574_PORT_ALL_HIGH;
    esp_err_t ret   = i2c_master_receive(p_ctrl->p_i2c_dev, &value, sizeof(value), PCF8574_I2C_TIMEOUT_MS); //
    if (ret == ESP_OK)                                                                                      //
    {
        /* Copy the port value while holding the mutex so the caller receives a consistent sample. */
        *p_data = value;
    }
    /* Release the transfer lock after the I2C operation and output update are complete. */ // Sai ne
#if USED_FREERTOS
    xSemaphoreGive(p_ctrl->p_i2c_transfer_mutex);
#endif
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Executes a sequence of PCF8574 port writes followed by port reads.
 *
 * This API is intended for matrix devices such as a keypad. Each element is transferred as a separate pair:
 * write p_write_values[i], then read the resulting port state into p_read_values[i]. In FreeRTOS builds, the mutex is
 * held for the whole sequence so another task cannot change the port between a write and its matching read. Without
 * FreeRTOS, the caller must serialize access.
 *
 * PCF8574 is a byte-oriented device without a register pointer, therefore a sequence is not sent as one multi-byte I2C
 * transaction.
 *
 * @param[in,out] p_ctrl         Pointer to an initialized runtime control block.
 * @param[in]     p_write_values Port values to write, one value per scan step.
 * @param[out]    p_read_values  Port samples, one value per scan step.
 * @param[in]     count          Number of write/read pairs. Zero is a no-op.
 *
 * @retval APP_SUCCESS                  The complete sequence succeeded.
 * @retval APP_ERR_INVALID_POINTER      The control pointer is NULL.
 * @retval APP_ERR_INVALID_ARGUMENT     A non-zero count has a NULL buffer.
 * @retval APP_ERR_INVALID_STATE        The control block has not been initialized.
 * @retval APP_ERR_TIMEOUT              The I2C mutex could not be acquired in a FreeRTOS build.
 * @return                              A mapped hardware or system error can also be returned.
 **********************************************************************************************************************/
app_err_t pcf8574_scan_sequence(pcf8574_instance_ctrl_t *const p_ctrl,
                                const uint8_t *const           p_write_values,
                                uint8_t *const                 p_read_values,
                                size_t                         count)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(NULL != p_ctrl->p_i2c_dev, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN((count == 0U) || (NULL != p_write_values && NULL != p_read_values), APP_ERR_INVALID_ARGUMENT);
#endif

    if (count == 0U)
    {
        return APP_SUCCESS;
    }

    /* Serialize the complete write/read sequence as one logical operation in FreeRTOS builds.
     * Calling pcf8574_write()/read() here would try to lock this mutex again. */
#if USED_FREERTOS
    if (xSemaphoreTake(p_ctrl->p_i2c_transfer_mutex, pdMS_TO_TICKS(PCF8574_I2C_TIMEOUT_MS)) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
#endif

    esp_err_t ret = ESP_OK;
    for (size_t i = 0U; i < count; i++)
    {
        const uint8_t write_value = p_write_values[i];
        uint8_t       read_value  = PCF8574_PORT_ALL_HIGH;

        ret = i2c_master_transmit(p_ctrl->p_i2c_dev, &write_value, sizeof(write_value), PCF8574_I2C_TIMEOUT_MS);
        if (ret != ESP_OK)
        {
            /* Samples completed before this index remain valid; this and all
             * later output entries are left unchanged. */
            break;
        }

        ret = i2c_master_receive(p_ctrl->p_i2c_dev, &read_value, sizeof(read_value), PCF8574_I2C_TIMEOUT_MS);
        if (ret != ESP_OK)
        {
            /* The write for this index reached the device, but no sample is
             * published unless its matching read also succeeds. */
            break;
        }

        p_read_values[i] = read_value;
    }

    /* The last successfully written value remains latched. A matrix-keypad
     * caller must restore its idle/interrupt-arm pattern after this API. */
#if USED_FREERTOS
    xSemaphoreGive(p_ctrl->p_i2c_transfer_mutex);
#endif
    return err_map_esp_to_app(ret);
}

/***********************************************************************************************************************
 * Waits for the interrupt worker to provide a port sample. The output pointer is optional.
 *
 * @param[in,out] p_ctrl      Pointer to the runtime control block.
 * @param[out]    p_data      Optional pointer that receives the interrupt sample.
 * @param[in]     wait_ticks  Maximum FreeRTOS ticks to wait; ignored without FreeRTOS.
 *
 * @retval APP_SUCCESS                 An interrupt sample was received.
 * @retval APP_ERR_INVALID_POINTER     The control pointer is NULL.
 * @retval APP_ERR_INVALID_STATE       The control block has not been initialized.
 * @retval APP_ERR_NOT_SUPPORTED       Interrupt processing is disabled.
 * @retval APP_ERR_TIMEOUT             No sample was received before the timeout.
 **********************************************************************************************************************/
app_err_t pcf8574_wait_interrupt(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t *const p_data, uint32_t wait_ticks)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ERROR_RETURN(true == p_ctrl->init, APP_ERR_INVALID_STATE);
#endif

#if USED_FREERTOS
    if (p_ctrl->p_intr_sem == NULL)
    {
        return APP_ERR_NOT_SUPPORTED;
    }
    /* intr_sem is binary: bursts are intentionally coalesced and p_data receives
     * the latest sample captured by the worker, not an event-by-event history. */
    if (xSemaphoreTake(p_ctrl->p_intr_sem, (TickType_t)wait_ticks) != pdTRUE)
    {
        return APP_ERR_TIMEOUT;
    }
    if (p_data != NULL)
    {
        *p_data = p_ctrl->intr_state;
    }
    return APP_SUCCESS;
#else
    (void)p_ctrl;
    (void)p_data;
    (void)wait_ticks;
    return APP_ERR_NOT_SUPPORTED;
#endif
}

/***********************************************************************************************************************

 * * Private functions

 * **********************************************************************************************************************/

/***********************************************************************************************************************

 * * Validates the initialization arguments and the interrupt configuration.

 * **********************************************************************************************************************/
static app_err_t pcf8574_validate_init_args(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg)
{
#if PCF8574_CFG_PARAMETER_CHECKING
    APP_ASSERT(NULL != p_ctrl);
    APP_ASSERT(NULL != p_cfg);

    const i2c_master_bus_config_t *p_bus_cfg = &p_cfg->i2c_bus_config;
    const i2c_device_config_t     *p_dev_cfg = &p_cfg->i2c_device_config;

    APP_ERROR_RETURN(false == p_ctrl->init, APP_ERR_INVALID_STATE);
    APP_ERROR_RETURN(
        (p_cfg->p_i2c_bus_handle != NULL)
            || ((p_bus_cfg->i2c_port == -1) || ((p_bus_cfg->i2c_port >= 0) && (p_bus_cfg->i2c_port < SOC_I2C_NUM))),
        APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((p_cfg->p_i2c_bus_handle != NULL)
                         || (GPIO_IS_VALID_GPIO(p_bus_cfg->sda_io_num) && GPIO_IS_VALID_GPIO(p_bus_cfg->scl_io_num)
                             && (p_bus_cfg->sda_io_num != p_bus_cfg->scl_io_num)),
                     APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(I2C_ADDR_BIT_LEN_7 == p_dev_cfg->dev_addr_length, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((p_dev_cfg->device_address >= PCF8574_I2C_ADDRESS_MIN)
                         && (p_dev_cfg->device_address <= PCF8574_I2C_ADDRESS_MAX),
                     APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((p_dev_cfg->scl_speed_hz > 0U) && (p_dev_cfg->scl_speed_hz <= PCF8574_I2C_SPEED_MAX_HZ),
                     APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN(false == p_dev_cfg->flags.disable_ack_check, APP_ERR_INVALID_ARGUMENT);
    APP_ERROR_RETURN((GPIO_NUM_NC == p_cfg->int_gpio) || (GPIO_IS_VALID_GPIO(p_cfg->int_gpio)),
                     APP_ERR_INVALID_ARGUMENT);
#if USED_FREERTOS
    if (p_cfg->int_gpio != GPIO_NUM_NC)
    {
        APP_ERROR_RETURN(GPIO_INTR_NEGEDGE == p_cfg->int_gpio_config.intr_type, APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN(GPIO_MODE_INPUT == p_cfg->int_gpio_config.mode, APP_ERR_INVALID_ARGUMENT);
        APP_ERROR_RETURN((1ULL << p_cfg->int_gpio) == p_cfg->int_gpio_config.pin_bit_mask, APP_ERR_INVALID_ARGUMENT);
    }
    else
    {
        APP_ERROR_RETURN(NULL == p_cfg->p_intr_cb, APP_ERR_INVALID_ARGUMENT);
    }
#endif

#else
    (void)p_ctrl;
    (void)p_cfg;
#endif

#if !USED_FREERTOS
    APP_ERROR_RETURN(GPIO_NUM_NC == p_cfg->int_gpio, APP_ERR_NOT_SUPPORTED);
#endif

    return APP_SUCCESS;
}

#if USED_FREERTOS
/***********************************************************************************************************************

 * * Creates the mutex used to serialize I2C transfers for this driver instance.

 * **********************************************************************************************************************/
static app_err_t pcf8574_create_i2c_transfer_mutex(pcf8574_instance_ctrl_t *const p_ctrl)
{
    p_ctrl->p_i2c_transfer_mutex = xSemaphoreCreateMutex();
    return (p_ctrl->p_i2c_transfer_mutex != NULL) ? APP_SUCCESS : APP_ERR_NO_MEMORY;
}
#endif

/***********************************************************************************************************************

 * * Attaches to the caller-provided I2C bus or creates a bus owned by this instance.

 * **********************************************************************************************************************/
static esp_err_t pcf8574_setup_i2c_bus(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg)
{
    p_ctrl->p_i2c_bus = p_cfg->p_i2c_bus_handle;
    if (p_ctrl->p_i2c_bus != NULL)
    {
        return ESP_OK;
    }

    esp_err_t ret = i2c_new_master_bus(&p_cfg->i2c_bus_config, &p_ctrl->p_i2c_bus);
    if (ret == ESP_OK)
    {
        p_ctrl->bus_created = true;
    }
    return ret;
}

/***********************************************************************************************************************

 * * Probes, registers, and releases all PCF8574 pins for quasi-bidirectional input operation.

 * **********************************************************************************************************************/
static esp_err_t pcf8574_setup_i2c_device(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg)
{
    esp_err_t ret
        = i2c_master_probe(p_ctrl->p_i2c_bus, p_cfg->i2c_device_config.device_address, PCF8574_I2C_TIMEOUT_MS);
    if (ret != ESP_OK)
    {
        ESP_LOGE(PCF8574_TAG, "No I2C device responded at address 0x%02X", p_cfg->i2c_device_config.device_address);
        /* Diagnostic-only fallback: report any responder to help identify an
         * incorrectly strapped address.
         * This may noticeably extend a failed init. */
        for (uint8_t addr = PCF8574_SCAN_FIRST_ADDR; addr < PCF8574_SCAN_END_ADDR; addr++)
        {
            if (i2c_master_probe(p_ctrl->p_i2c_bus, addr, PCF8574_SCAN_TIMEOUT_MS) == ESP_OK)
            {
                ESP_LOGW(PCF8574_TAG, "Responding I2C device found at address 0x%02X", addr);
            }
        }
        return ret;
    }

    ret = i2c_master_bus_add_device(p_ctrl->p_i2c_bus, &p_cfg->i2c_device_config, &p_ctrl->p_i2c_dev);
    if (ret != ESP_OK)
    {
        return ret;
    }

    const uint8_t init_value = PCF8574_PORT_ALL_HIGH; // hihi
    ret = i2c_master_transmit(p_ctrl->p_i2c_dev, &init_value, sizeof(init_value), PCF8574_I2C_TIMEOUT_MS);
    if (ret == ESP_OK)
    {
        p_ctrl->intr_state = init_value;
    }
    return ret;
}

#if USED_FREERTOS
/***********************************************************************************************************************

 * * Configures the GPIO interrupt and starts the task that reads and publishes port samples.

 * **********************************************************************************************************************/
static esp_err_t pcf8574_setup_interrupt(pcf8574_instance_ctrl_t *const p_ctrl, const pcf8574_config_t *const p_cfg)
{
    if (p_cfg->int_gpio == GPIO_NUM_NC)
    {
        return ESP_OK;
    }

    p_ctrl->p_intr_sem = xSemaphoreCreateBinary();
    if (p_ctrl->p_intr_sem == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = gpio_config(&p_cfg->int_gpio_config);
    if (ret != ESP_OK)
    {
        return ret;
    }

    ret = gpio_install_isr_service(0); // macro?!
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        return ret;
    }

    if (xTaskCreate(pcf8574_intr_task,
                    "pcf8574_intr",
                    PCF8574_INTR_STACK_SIZE,
                    p_ctrl,
                    PCF8574_INTR_TASK_PRIORITY,
                    &p_ctrl->p_intr_task)
        != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    /* The worker may run as soon as the ISR is registered, so mark the instance ready first. */
    p_ctrl->init = true;
    ret          = gpio_isr_handler_add(p_cfg->int_gpio, pcf8574_gpio_isr_handler, p_ctrl);
    if (ret == ESP_OK)
    {
        p_ctrl->isr_added = true;
    }
    return ret;
}
#endif

/***********************************************************************************************************************

 * * Releases resources acquired by an instance and returns the first ESP-IDF cleanup error.
 *
 * @param[in,out] p_ctrl  Pointer to the runtime control block.
 *
 * @retval ESP_OK  All resources were released.
 * @return         The first ESP-IDF GPIO or I2C cleanup error.
 **********************************************************************************************************************/
static esp_err_t pcf8574_release(pcf8574_instance_ctrl_t *const p_ctrl)
{
    esp_err_t ret = ESP_OK;

#if USED_FREERTOS
    if (p_ctrl->isr_added)
    {
        esp_err_t cleanup_ret = gpio_isr_handler_remove(p_ctrl->p_cfg->int_gpio);
        if (ret == ESP_OK && cleanup_ret != ESP_OK)
        {
            ret = cleanup_ret;
        }
        p_ctrl->isr_added = false;
    }
    if (p_ctrl->p_intr_task != NULL)
    {
        /* The current implementation force-deletes the worker. The caller must
         * serialize deinit against interrupt handling and callbacks. */
        vTaskDelete(p_ctrl->p_intr_task);
        p_ctrl->p_intr_task = NULL;
    }
    if (p_ctrl->p_intr_sem != NULL)
    {
        vSemaphoreDelete(p_ctrl->p_intr_sem);
        p_ctrl->p_intr_sem = NULL;
    }
#endif

    if (p_ctrl->p_i2c_dev != NULL)
    {
        esp_err_t cleanup_ret = i2c_master_bus_rm_device(p_ctrl->p_i2c_dev);
        if (ret == ESP_OK && cleanup_ret != ESP_OK)
        {
            ret = cleanup_ret;
        }
        p_ctrl->p_i2c_dev = NULL;
    }
    if (p_ctrl->bus_created && p_ctrl->p_i2c_bus != NULL) //
    {
        esp_err_t cleanup_ret = i2c_del_master_bus(p_ctrl->p_i2c_bus);
        if (ret == ESP_OK && cleanup_ret != ESP_OK)
        {
            ret = cleanup_ret;
        }
    }
    p_ctrl->p_i2c_bus   = NULL;
    p_ctrl->bus_created = false;
#if USED_FREERTOS
    if (p_ctrl->p_i2c_transfer_mutex != NULL)
    {
        vSemaphoreDelete(p_ctrl->p_i2c_transfer_mutex);
        p_ctrl->p_i2c_transfer_mutex = NULL;
    }
#endif
    p_ctrl->p_cfg = NULL;
    p_ctrl->init  = false;
    return ret;
}

/***********************************************************************************************************************
 * @brief Notifies only the worker belonging to the interrupting device instance.
 **********************************************************************************************************************/
#if USED_FREERTOS
static void IRAM_ATTR pcf8574_gpio_isr_handler(void *p_api_ctrl)
{
    pcf8574_instance_ctrl_t *p_ctrl     = (pcf8574_instance_ctrl_t *)p_api_ctrl;
    BaseType_t               task_woken = pdFALSE;
    if (p_ctrl->p_intr_task != NULL)
    {
        /* Never perform I2C from ISR context; only wake the instance worker. */
        vTaskNotifyGiveFromISR(p_ctrl->p_intr_task, &task_woken);
        portYIELD_FROM_ISR(task_woken); //
    }
}

/***********************************************************************************************************************
 * @brief Reads the interrupting instance, publishes its sample, and invokes its configured callback.
 **********************************************************************************************************************/
static void pcf8574_intr_task(void *p_api_ctrl)
{
    pcf8574_instance_ctrl_t *p_ctrl = (pcf8574_instance_ctrl_t *)p_api_ctrl;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint8_t value = PCF8574_PORT_ALL_LOW;
        if (pcf8574_read(p_ctrl, &value) == APP_SUCCESS)
        {
            /* Reading the PCF8574 samples the pins and acknowledges its active
             * interrupt output before publishing the event to consumers. */
            p_ctrl->intr_state = value;
            xSemaphoreGive(p_ctrl->p_intr_sem);
            if (p_ctrl->p_cfg->p_intr_cb != NULL)
            {
                /* Callback context is this worker task, not the GPIO ISR. Keep
                 * callbacks short so subsequent pin changes are serviced promptly. */
                p_ctrl->p_cfg->p_intr_cb(value, p_ctrl->p_cfg->p_user_ctx);
            }
            else
            {
                ESP_LOGW(PCF8574_TAG, "Interrupt received, port state: 0x%02X", value);
            }
        }
    }
}
#endif
