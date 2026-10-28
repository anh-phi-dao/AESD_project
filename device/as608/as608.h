#ifndef AS608_H_
#define AS608_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#include "uart.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*******************************************************************************************************************
     * Macro definitions
     ******************************************************************************************************************/

/* Confirmation codes returned by the functions below (module codes, plus one local error). */
#define AS608_OK             0x00U /* command completed */
#define AS608_ERR_NO_FINGER  0x02U /* GenImg: no finger on the sensor */
#define AS608_ERR_NO_MATCH   0x09U /* Search: no matching template in the library */
#define AS608_ERR_COMM       0xFFU /* no / malformed response from the module (local, not a module code) */

    /*******************************************************************************************************************
     * Public APIs
     * Every command takes the module address (4 bytes) and returns the confirmation code of the response.
     ******************************************************************************************************************/

    /** Reads one response packet and returns its confirmation code. p_value (optional) receives the first 16-bit
     *  value of the response data (template count for TempleteNum, matched page id for Search). */
    uint8_t DEV_AS608_Reponse(uart_port_num_t e_uart_port, uint8_t instruction_code, uint16_t *p_value);

    /** Detects a finger and stores its image in the image buffer. */
    uint8_t DEV_AS608_GenImg(uart_port_num_t e_uart_port, uint8_t *p_AS608_address);

    /** Generates a character file from the image buffer into character buffer 1 or 2. */
    uint8_t DEV_AS608_Img2Tz(uart_port_num_t e_uart_port, uint8_t *p_AS608_address, uint8_t *p_buffer_id);

    /** Searches the library for the template in the given character buffer. p_matched_page receives the page id. */
    uint8_t DEV_AS608_Search(uart_port_num_t e_uart_port,
                             uint8_t        *p_AS608_address,
                             uint8_t        *p_buffer_id,
                             uint8_t        *p_start_page,
                             uint8_t        *p_page_number,
                             uint16_t       *p_matched_page);

    /** Merges character buffers 1 and 2 into a template. */
    uint8_t DEV_AS608_RegModel(uart_port_num_t e_uart_port, uint8_t *p_AS608_address);

    /** Stores the template of the given character buffer at page_id of the module flash. */
    uint8_t DEV_AS608_Store(uart_port_num_t e_uart_port,
                            uint8_t        *p_AS608_address,
                            uint8_t        *p_buffer_id,
                            uint8_t        *p_page_id);

    /** Reads the number of stored templates. */
    uint8_t DEV_AS608_TempleteNum(uart_port_num_t e_uart_port, uint8_t *p_AS608_address, uint16_t *p_template_count);

    /** Deletes N templates starting from page_id. */
    uint8_t DEV_AS608_DeleteChar(uart_port_num_t e_uart_port,
                                 uint8_t        *p_AS608_address,
                                 uint8_t        *p_page_id,
                                 uint8_t        *p_number_of_templates);

#ifdef __cplusplus
}
#endif

#endif /* AS608_H_ */
