#ifndef INA219_CONFIG_H_
#define INA219_CONFIG_H_

/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Schematic rev3: module on J6, A0 = A1 = GND, shares I2C_NUM_0 with the PN532 (0x24) and the PCF8574 (0x20). */
#define INA219_I2C_ADDRESS    0x40U
#define INA219_I2C_SPEED_HZ   100000U
#define INA219_I2C_TIMEOUT_MS 50

/* Shunt 0.1 ohm, PGA /8 (+-320 mV -> +-3.2 A), BRNG 32 V (3S pack up to 12.6 V).
 * Current_LSB = 100 uA -> CAL = trunc(0.04096 / (Current_LSB * R_shunt)) = 4096, Power_LSB = 20 * Current_LSB. */
#define INA219_CURRENT_LSB_UA 100
#define INA219_CALIBRATION    4096U

/* BRNG = 32 V, PG = /8, BADC = SADC = 12 bit x 128 samples (68 ms), shunt and bus continuous. */
#define INA219_CONFIG_VALUE   0x3FFFU

#endif /* INA219_CONFIG_H_ */
