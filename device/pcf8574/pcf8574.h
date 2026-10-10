#ifndef PCF8574_H_
#define PCF8574_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "pcf8574_config.h"
#include "app_err.h"
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

#if USED_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* PCF8574 uses quasi-bidirectional pins: writing 1 releases a pin so it can be
 * read as an input, while writing 0
 * actively sinks the pin LOW. */
#define PCF8574_PORT_ALL_HIGH UINT8_MAX
#define PCF8574_PORT_ALL_LOW  0x00U

/***********************************************************************************************************************

 * * Typedef definitions
 **********************************************************************************************************************/

typedef void (*pcf8574_intr_callback_t)(uint8_t pin_state, void *p_user_ctx);

/* Configuration remains caller-owned and must stay valid and unchanged until deinit. */
typedef struct st_pcf8574_config
{
    i2c_master_bus_handle_t p_i2c_bus_handle;  /* NULL: create a bus; otherwise borrow this bus. */
    i2c_master_bus_config_t i2c_bus_config;    /* Bus configuration used when creating the I2C bus. */
    i2c_device_config_t     i2c_device_config; /* PCF8574 device configuration. */
    gpio_num_t              int_gpio;          /* GPIO_NUM_NC disables interrupts; required without FreeRTOS. */
    gpio_config_t           int_gpio_config;   /* Must describe int_gpio; INT is normally active-LOW/open-drain. */
    pcf8574_intr_callback_t p_intr_cb;         /* Callback used by the FreeRTOS interrupt worker. Check lai ii*/
    void                   *p_user_ctx;        /* Context passed unchanged to p_intr_cb. */
} pcf8574_config_t;

/* Caller-owned runtime state. Zero-initialize before first use; do not copy an active control block. */
typedef struct st_pcf8574_instance_ctrl
{
    i2c_master_bus_handle_t p_i2c_bus; /* I2C bus borrowed or created by this instance. */
    i2c_master_dev_handle_t p_i2c_dev; /* I2C device handle registered for the PCF8574 address. */
#if USED_FREERTOS
    TaskHandle_t      p_intr_task;          /* FreeRTOS task that services interrupt notifications. */
    SemaphoreHandle_t p_i2c_transfer_mutex; /* Mutex serializing I2C transfers. */
    SemaphoreHandle_t p_intr_sem;           /* Semaphore notifying the interrupt worker of an event. */
#endif
    const pcf8574_config_t *p_cfg;       /* Caller-owned immutable configuration. */
    uint8_t                 intr_state;  /* Latest sample; multiple pending interrupts may be coalesced. */
    bool                    bus_created; /* True when the driver created and owns the I2C bus. */
#if USED_FREERTOS
    bool isr_added; /* True when the GPIO ISR handler was installed. */
#endif
    bool init; /* True after successful initialization. */
} pcf8574_instance_ctrl_t;

/***********************************************************************************************************************
 * Exported global variables
 **********************************************************************************************************************/

/***********************************************************************************************************************
 * Public function prototypes
 **********************************************************************************************************************/

/* Keep p_ctrl and p_cfg alive until deinit. Serialize init/deinit against all API calls and callbacks.
 * Full function descriptions and operation comments are beside the implementations in pcf8574.c. */
#ifdef __cplusplus
extern "C"
{
#endif

    /* Attach to/create the I2C bus, register the PCF8574, and optionally start INT processing. */
    app_err_t pcf8574_init(pcf8574_instance_ctrl_t *const p_ctrl,
                           const pcf8574_config_t *const  p_cfg,
                           bool                           i2c_bus_first_start_up);

    /* Stop interrupt processing and release resources owned by this instance. */
    app_err_t pcf8574_deinit(pcf8574_instance_ctrl_t *const p_ctrl);

    /* Write or sample all eight PCF8574 pins in one I2C transaction. */
    app_err_t pcf8574_write(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t data);
    app_err_t pcf8574_read(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t *const p_data);

    /* Perform count atomic write/read pairs. The final write value remains latched after return. */
    app_err_t pcf8574_scan_sequence(pcf8574_instance_ctrl_t *const p_ctrl,
                                    const uint8_t *const           p_write_values,
                                    uint8_t *const                 p_read_values,
                                    size_t                         count);

    /* Wait for the interrupt worker's latest sample; returns APP_ERR_NOT_SUPPORTED without FreeRTOS. */
    app_err_t pcf8574_wait_interrupt(pcf8574_instance_ctrl_t *const p_ctrl, uint8_t *const p_data, uint32_t wait_ticks);

#ifdef __cplusplus
}
#endif

#endif /* PCF8574_H_ */
