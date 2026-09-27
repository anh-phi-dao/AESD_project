#ifndef NETWORK_MANAGER_CONFIG_H_
#define NETWORK_MANAGER_CONFIG_H_

/***********************************************************************************************************************
 * Configuration macros
 **********************************************************************************************************************/

/* Disabled checks require valid pointers/configuration and correct init/deinit ordering from the caller. */
#ifndef NETWORK_MANAGER_CFG_PARAMETER_CHECKING
#define NETWORK_MANAGER_CFG_PARAMETER_CHECKING 1
#endif

#if (NETWORK_MANAGER_CFG_PARAMETER_CHECKING != 0) && (NETWORK_MANAGER_CFG_PARAMETER_CHECKING != 1)
#error "NETWORK_MANAGER_CFG_PARAMETER_CHECKING must be 0 or 1"
#endif

/* Reconnect delay doubles after every failed attempt, between these bounds. */
#ifndef NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS
#define NETWORK_MANAGER_DEFAULT_RECONNECT_MIN_MS 1000U
#endif

#ifndef NETWORK_MANAGER_DEFAULT_RECONNECT_MAX_MS
#define NETWORK_MANAGER_DEFAULT_RECONNECT_MAX_MS 60000U
#endif

/* POSIX TZ string used when the configuration leaves it NULL. "ICT-7" is Vietnam time (UTC+7). */
#ifndef NETWORK_MANAGER_DEFAULT_TIMEZONE
#define NETWORK_MANAGER_DEFAULT_TIMEZONE "ICT-7"
#endif

#ifndef NETWORK_MANAGER_DEFAULT_NTP_SERVER
#define NETWORK_MANAGER_DEFAULT_NTP_SERVER "pool.ntp.org"
#endif

/* The clock counts as valid once it is past 2025-01-01 00:00:00 UTC. It survives a soft reset, so TLS can start
 * before the first SNTP reply in that case. */
#define NETWORK_MANAGER_MIN_VALID_EPOCH    1735689600

/* 802.11 limits. WPA2/WPA3 personal passphrases are 8..63 characters, or 64 hex digits. */
#define NETWORK_MANAGER_SSID_MAX_LEN       32U
#define NETWORK_MANAGER_PASSWORD_MIN_LEN   8U
#define NETWORK_MANAGER_PASSWORD_MAX_LEN   64U

#endif /* NETWORK_MANAGER_CONFIG_H_ */
