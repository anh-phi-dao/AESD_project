#ifndef DOOR_SIM_CONFIG_H_
#define DOOR_SIM_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init ordering from the caller. */
#ifndef DOOR_SIM_CFG_PARAMETER_CHECKING
#define DOOR_SIM_CFG_PARAMETER_CHECKING 1
#endif

#if (DOOR_SIM_CFG_PARAMETER_CHECKING != 0) && (DOOR_SIM_CFG_PARAMETER_CHECKING != 1)
#error "DOOR_SIM_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* Holding the button at least this long raises a tamper alert; a shorter press makes an access attempt. */
#ifndef DOOR_SIM_LONG_PRESS_MS
#define DOOR_SIM_LONG_PRESS_MS 1000U
#endif

#define DOOR_SIM_BUTTON_POLL_MS    20U
#define DOOR_SIM_BUTTON_TASK_STACK 3072U
#define DOOR_SIM_BUTTON_TASK_PRIO  3U

/* The console task runs the commands, which only write to the event log. */
#define DOOR_SIM_CONSOLE_STACK 4096U
#define DOOR_SIM_CONSOLE_PRIO  2U
#define DOOR_SIM_PROMPT        "smartlock> "

/* "sim seed" without a count, and the largest count accepted. */
#define DOOR_SIM_SEED_DEFAULT 10U
#define DOOR_SIM_SEED_MAX     20U

#endif /* DOOR_SIM_CONFIG_H_ */
