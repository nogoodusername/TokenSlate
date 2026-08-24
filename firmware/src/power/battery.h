#pragma once

#include <stdint.h>

enum PowerState {
    POWER_ON_BATTERY,
    POWER_ON_USB,
};

/* Placeholder — calibrate against a multimeter on the actual unit.
 * No real single-cell LiPo reads this high; see docs §14. */
extern float USB_PRESENT_THRESHOLD_V;

#define LOW_BATTERY_THRESHOLD_PERCENT 15

/* GPIO4 raw voltage, doubled for the ~2:1 divider. Meaning depends on
 * power source — see readPowerState() and docs §14. */
float readBatteryVoltage();

PowerState readPowerState();

/* Only meaningful when readPowerState() == POWER_ON_BATTERY. */
uint8_t batteryVoltageToPercent(float voltage);
