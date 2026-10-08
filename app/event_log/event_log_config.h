#ifndef EVENT_LOG_CONFIG_H_
#define EVENT_LOG_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init ordering from the caller. */
#ifndef EVENT_LOG_CFG_PARAMETER_CHECKING
#define EVENT_LOG_CFG_PARAMETER_CHECKING 1
#endif

#if (EVENT_LOG_CFG_PARAMETER_CHECKING != 0) && (EVENT_LOG_CFG_PARAMETER_CHECKING != 1)
#error "EVENT_LOG_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* Number of most recent access events kept. They are served by history/req and published in the retained "recent"
 * topic, so the web sees them even while the lock is offline. The SD card log will hold the full history. */
#ifndef EVENT_LOG_EVENT_CAPACITY
#define EVENT_LOG_EVENT_CAPACITY 20U
#endif

/* Number of most recent alerts kept for the retained "recent" topic. */
#ifndef EVENT_LOG_ALERT_CAPACITY
#define EVENT_LOG_ALERT_CAPACITY 10U
#endif

/* Buffer sizes including the terminator. Longer strings are truncated on a UTF-8 character boundary. */
#ifndef EVENT_LOG_USER_SIZE
#define EVENT_LOG_USER_SIZE 32U
#endif

#ifndef EVENT_LOG_ALERT_MSG_SIZE
#define EVENT_LOG_ALERT_MSG_SIZE 96U
#endif

/* The whole log is one blob in the main "nvs" partition (about 2 KB), rewritten on every entry. NVS spreads the
 * writes over its pages, which lasts decades at a door's event rate. */
#ifndef EVENT_LOG_NVS_NAMESPACE
#define EVENT_LOG_NVS_NAMESPACE "event_log"
#endif

#if (EVENT_LOG_EVENT_CAPACITY == 0U) || (EVENT_LOG_ALERT_CAPACITY == 0U)
#error "EVENT_LOG_EVENT_CAPACITY and EVENT_LOG_ALERT_CAPACITY must be greater than zero"
#endif

#endif /* EVENT_LOG_CONFIG_H_ */
