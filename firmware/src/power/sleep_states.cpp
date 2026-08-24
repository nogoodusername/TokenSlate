#include "sleep_states.h"

#include <Arduino.h>
#include <esp_sleep.h>

#include "../board_pins.h"

RTC_DATA_ATTR static uint32_t boot_count = 0;

const char *resetReasonName(esp_reset_reason_t r)
{
    switch (r) {
        case ESP_RST_POWERON:   return "POWERON";
        case ESP_RST_EXT:       return "EXT";
        case ESP_RST_SW:        return "SW";
        case ESP_RST_PANIC:     return "PANIC";
        case ESP_RST_INT_WDT:   return "INT_WDT";
        case ESP_RST_TASK_WDT:  return "TASK_WDT";
        case ESP_RST_WDT:       return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_SDIO:      return "SDIO";
        default:                return "UNKNOWN";
    }
}

uint32_t recordBootAndPrintWakeReason()
{
    boot_count++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reason = esp_reset_reason();

    /* Serial (native USB-CDC) blocks on write/flush when no host has the
     * port open (e.g. battery-only, no cable) — guard every call with
     * `if (Serial)` or the whole sketch hangs here. */
    if (Serial) {
        Serial.printf("[TokenSlate] boot #%lu, reset reason: %s, wake cause: %d\n",
                      (unsigned long)boot_count, resetReasonName(reason), (int)cause);
        if (cause == ESP_SLEEP_WAKEUP_EXT1) {
            uint64_t mask = esp_sleep_get_ext1_wakeup_status();
            Serial.printf("[TokenSlate] EXT1 wake mask: 0x%llx\n", mask);
        }
    }

    return boot_count;
}

static void debugStep(const char *step)
{
    if (Serial) {
        Serial.printf("[TokenSlate] sleep step: %s\n", step);
        Serial.flush();
    }
}

void enterSleep(TFT_eSPI &tft)
{
    debugStep("start");

    tft.writecommand(0x10); /* SLPIN */
    debugStep("SLPIN sent");
    delay(120);
    debugStep("SLPIN delay done");

    digitalWrite(PIN_LCD_BL, LOW);
    debugStep("backlight off");
    digitalWrite(PIN_POWER_ON, LOW);
    debugStep("rail cut");

    /* Both buttons are active-low (pulled up, pressed = LOW). ANY_HIGH
     * would wake immediately since GPIO0 idles HIGH; per §3, use ANY_LOW
     * so either button wakes the device. */
    esp_sleep_enable_ext1_wakeup(
        (1ULL << PIN_BUTTON_1) | (1ULL << PIN_BUTTON_2),
        ESP_EXT1_WAKEUP_ANY_LOW);
    debugStep("ext1 wake armed");

    esp_deep_sleep_start();
}

WakeSource getWakeSource()
{
    if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1) {
        return WAKE_SOURCE_NONE;
    }
    uint64_t mask = esp_sleep_get_ext1_wakeup_status();
    bool boot = mask & (1ULL << PIN_BUTTON_1);
    bool key  = mask & (1ULL << PIN_BUTTON_2);
    if (boot && key) return WAKE_SOURCE_BOTH;
    if (boot) return WAKE_SOURCE_BOOT;
    if (key)  return WAKE_SOURCE_KEY;
    return WAKE_SOURCE_NONE;
}
