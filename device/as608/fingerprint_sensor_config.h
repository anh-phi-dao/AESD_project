#ifndef FINGERPRINT_SENSOR_CONFIG_H_
#define FINGERPRINT_SENSOR_CONFIG_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include "uart.h"

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Verified on hardware: ESP TX = IO9 -> AS608 RX, ESP RX = IO8 <- AS608 TX. */
#define FINGERPRINT_UART_PORT       UART_PORT_NUM_1
#define FINGERPRINT_UART_TX_PIN     GPIO_NUM_9
#define FINGERPRINT_UART_RX_PIN     GPIO_NUM_8
#define FINGERPRINT_UART_BAUDRATE   UART_BAUDRATE_57600 /* AS608 factory default */

/* Factory default module address. */
#define FINGERPRINT_MODULE_ADDRESS  {0xFF, 0xFF, 0xFF, 0xFF}

#define FINGERPRINT_LIBRARY_SIZE    162U /* AS608 flash capacity */
#define FINGERPRINT_POLL_PERIOD_MS  100U
#define FINGERPRINT_WAIT_TIMEOUT_MS 10000U

#endif /* FINGERPRINT_SENSOR_CONFIG_H_ */
