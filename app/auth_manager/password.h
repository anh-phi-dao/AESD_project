#ifndef PASSWORD_H_

#define PASSWORD_H_

/***********************************************************************************************************************
 * Includes   <System Includes> , "Project Includes"
 **********************************************************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_err.h"
#include "esp_log.h"
#include "keypad.h"

/*******************************************************************************************************************
 * Macro definitions
 ******************************************************************************************************************/

#define PASSWORD_DEBUG_TAG  "AUTHEN_PASSWORD"
#define MAX_PASSWORD_LENGTH 32

/*******************************************************************************************************************
 * Typedef definitions
 ******************************************************************************************************************/

struct password_handle
{
    uint16_t current_index;
    bool     full;
    char     input_password[MAX_PASSWORD_LENGTH];
    char     password[MAX_PASSWORD_LENGTH];
};

typedef struct password_handle password_handle_t;

enum
{
    ERROR_FILLING_PASSWORD = -1,
    FILLING_PASSWORD,
    FILLING_ENOUGH,
    NO_MORE_SPACE
};

enum
{
    AUTHEN_SUCCESS,
    AUTHEN_FAILED,
    AUTHEN_NONE
};

/*******************************************************************************************************************
 * Public APIs
 ******************************************************************************************************************/

/* Initialize the device password. */
app_err_t init_password(password_handle_t *device_password, uint16_t length, const char *init_password);

/* Fill the password from keypad input and return the authentication status. */
uint8_t fill_password(char key, password_handle_t *device_password);

uint8_t validate_password(password_handle_t *device_password);

app_err_t change_password(password_handle_t *device_password);

/* Deinitialize the device password. */
app_err_t deinit_password(password_handle_t *device_password);

#endif
