#ifndef PCF8574_CONFIG_H_
#define PCF8574_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Override with -DUSED_FREERTOS=0 to omit FreeRTOS synchronization and interrupt-worker code.
 * Keep this value consistent across every translation unit that includes pcf8574.h. */
#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if ((USED_FREERTOS != 0) && (USED_FREERTOS != 1))
#error "USED_FREERTOS must be 0 or 1"
#endif

/* Disabled checks require valid pointers/configuration and correct init/deinit ordering from the caller. */
#ifndef PCF8574_CFG_PARAMETER_CHECKING
#define PCF8574_CFG_PARAMETER_CHECKING 1
#endif

#if (PCF8574_CFG_PARAMETER_CHECKING != 0) && (PCF8574_CFG_PARAMETER_CHECKING != 1)
#error "PCF8574_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* PCF8574 device limits from the datasheet. */
#define PCF8574_I2C_ADDRESS_MIN    0x20U
#define PCF8574_I2C_ADDRESS_MAX    0x27U
#define PCF8574_I2C_SPEED_MAX_HZ   100000U

#endif /* PCF8574_CONFIG_H_ */
