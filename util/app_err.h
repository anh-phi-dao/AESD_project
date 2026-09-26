#ifndef APP_ERR_H
#define APP_ERR_H

/***********************************************************************************************************************

 * * Determines if a C++ compiler is being used.

 * ********************************************************************************************************************/
#if defined(__cplusplus)
#define ST_CPP_HEADER \
    extern "C"        \
    {
#define ST_CPP_FOOTER }
#else
#define ST_CPP_HEADER
#define ST_CPP_FOOTER
#endif

ST_CPP_HEADER

typedef enum e_app_err
{
    APP_SUCCESS = 0,

    /* Logic & Parameter Errors */
    APP_ERR_INVALID_ARGUMENT = 1, // Invalid input parameter
    APP_ERR_INVALID_POINTER  = 2, // NULL pointer
    APP_ERR_INVALID_STATE    = 3, // Function called before module initialization

    /* Hardware & Communication Errors */
    APP_ERR_TIMEOUT       = 10, // Wait timeout exceeded
    APP_ERR_NOT_FOUND     = 11, // Data not found
    APP_ERR_NO_MEMORY     = 12, // Out of memory
    APP_ERR_HW_LOCKED     = 13, // Solenoid busy or locked
    APP_ERR_NOT_SUPPORTED = 14, // Requested operation is not supported

    /* System Errors */
    APP_FAIL = 255 // Unspecified general error
} app_err_t;

#define APP_ASSERT(condition)               \
    do                                      \
    {                                       \
        if (!(condition))                   \
        {                                   \
            return APP_ERR_INVALID_POINTER; \
        }                                   \
    } while (0)

#define APP_ERROR_RETURN(condition, error_code) \
    do                                          \
    {                                           \
        if (!(condition))                       \
        {                                       \
            return (error_code);                \
        }                                       \
    } while (0)

ST_CPP_FOOTER

#endif /* APP_ERR_H */
