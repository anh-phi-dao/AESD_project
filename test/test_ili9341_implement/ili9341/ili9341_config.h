#ifndef ILI9341_CONFIG_H_
#define ILI9341_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Override with -DUSED_FREERTOS=0 to omit FreeRTOS synchronization code.
 * Keep this value consistent across every translation unit that includes ili9341.h. */
#ifndef USED_FREERTOS
#define USED_FREERTOS 1
#endif

#if ((USED_FREERTOS != 0) && (USED_FREERTOS != 1))
#error "USED_FREERTOS must be 0 or 1"
#endif

/* Disabled checks require valid pointers/configuration and correct init/deinit ordering from the caller. */
#ifndef ILI9341_CFG_PARAMETER_CHECKING
#define ILI9341_CFG_PARAMETER_CHECKING 1
#endif

#if (ILI9341_CFG_PARAMETER_CHECKING != 0) && (ILI9341_CFG_PARAMETER_CHECKING != 1)
#error "ILI9341_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* ILI9341 display resolution limits. */
#define ILI9341_HOR_RES_MAX        240U
#define ILI9341_VER_RES_MAX        320U

#endif /* ILI9341_CONFIG_H_ */