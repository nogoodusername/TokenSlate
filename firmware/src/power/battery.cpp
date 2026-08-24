#include "battery.h"

#include <Arduino.h>
#include <driver/adc.h>

#include "../board_pins.h"

/* GPIO4 == ADC1_CHANNEL_3 on ESP32-S3 (GPIO1..10 -> ADC1 CH0..9). */
#define BAT_ADC_CHANNEL ADC1_CHANNEL_3

float USB_PRESENT_THRESHOLD_V = 4.4f;

struct VoltagePoint {
    float voltage;
    uint8_t percent;
};

/* Single-cell LiPo discharge curve, highest voltage first. See docs §14. */
static const VoltagePoint kCurve[] = {
    {4.20f, 100},
    {3.98f, 80},
    {3.87f, 60},
    {3.79f, 40},
    {3.68f, 20},
    {3.45f, 0},
};

static bool s_adcInitialized = false;

float readBatteryVoltage()
{
    /* Arduino's analogRead()/analogReadMilliVolts() call pinMode(pin,
     * ANALOG) internally on *every* read, re-touching the GPIO mux each
     * time -- hangs the device later during deep-sleep entry, but only
     * when USB is attached (never reproduced on battery-only power).
     * Configuring the IDF ADC1 driver once and reading raw afterwards
     * avoids that repeated reconfiguration. */
    if (!s_adcInitialized) {
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(BAT_ADC_CHANNEL, ADC_ATTEN_DB_11);
        s_adcInitialized = true;
    }
    int raw = adc1_get_raw(BAT_ADC_CHANNEL);
    return (raw / 4095.0f) * 3.3f * 2.0f;
}

PowerState readPowerState()
{
    return (readBatteryVoltage() > USB_PRESENT_THRESHOLD_V) ? POWER_ON_USB : POWER_ON_BATTERY;
}

uint8_t batteryVoltageToPercent(float voltage)
{
    constexpr size_t n = sizeof(kCurve) / sizeof(kCurve[0]);

    if (voltage >= kCurve[0].voltage) return kCurve[0].percent;
    if (voltage <= kCurve[n - 1].voltage) return kCurve[n - 1].percent;

    for (size_t i = 0; i + 1 < n; i++) {
        const VoltagePoint &hi = kCurve[i];
        const VoltagePoint &lo = kCurve[i + 1];
        if (voltage <= hi.voltage && voltage >= lo.voltage) {
            float t = (voltage - lo.voltage) / (hi.voltage - lo.voltage);
            return (uint8_t)(lo.percent + t * (hi.percent - lo.percent));
        }
    }
    return 0;
}
