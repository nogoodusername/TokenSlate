#include <Arduino.h>
#include <TFT_eSPI.h>

#include "board_pins.h"
#include "power/battery.h"
#include "power/sleep_states.h"
#include "ui/screens.h"

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 1: display bring-up                         */
/*  Confirms PIN_POWER_ON/backlight sequencing on cold boot and after  */
/*  wake-from-sleep, on battery power. Sleep sequence follows §3.      */
/* ------------------------------------------------------------------ */

#define AWAKE_MS 10000UL

TFT_eSPI tft = TFT_eSPI();

void setup()
{
    Serial.begin(115200);
    /* Never block on Serial output. Without this, HWCDC writes can still
     * stall (bounded, but adds up across calls) once a host driver merely
     * *attaches* (e.g. USB plugged in with no active monitor reading) --
     * `if (Serial)` alone isn't enough, since that's true in that case too. */
    Serial.setTxTimeoutMs(0);
    delay(50);

    /* Sample the battery ADC before touching the LCD bus at all -- see
     * docs §3 / power/battery.cpp for the known intermittent-freeze issue
     * this can trigger when a USB host is attached but idle. */
    float batteryVoltage = readBatteryVoltage();

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    uint32_t bootCount = recordBootAndPrintWakeReason();

    initDisplay(tft);
    renderBringupScreen(tft, bootCount, batteryVoltage);

    if (Serial) {
        Serial.println("[TokenSlate] awake, sleeping in 10s");
    }
}

void loop()
{
    static unsigned long start = millis();
    if (millis() - start >= AWAKE_MS) {
        enterSleep(tft);
    }
    delay(50);
}
