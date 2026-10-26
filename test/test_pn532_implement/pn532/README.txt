PN532 debug test
================

Purpose
-------
Test the specific case where ESP32 is reset/flashed while PN532 remains powered.

Changes
-------
1. PN532 reset pin is GPIO5 (RST_RF from the supplied schematic).
2. pn532_init() performs a real hardware reset of PN532 on every ESP32 boot.
3. The test only scans UID. It does NOT call pn532_in_list_passive_target() a second time.
4. Automatic timeout-recovery inside pn532_write_command() is disabled in this debug driver
   so the test is deterministic.

How to use
----------
- Replace your current pn532_driver.c with the one in this folder.
- Use test_pn532_reset_scan.c as app_main (or copy its app_main into your test project).
- Keep pn532.c/.h and pn532_driver_i2c.c/.h unchanged for this first test.
- Flash and monitor.
- Present the same card.
- Remove/re-present the card.
- Reset ESP32 or flash again without power-cycling PN532.
- The log should show:
    Hardware reset PN532 on GPIO 5 ...
    Hardware reset PN532 done
    init ok
    Found chip PN532
    Firmware ver. 1.6
  and then the UID when the card is presented.

If GPIO5 is not actually connected to RST_RF on your assembled PCB, change PN532_RESET_PIN.
