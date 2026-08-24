#pragma once

#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <esp_system.h>

/* Call once at the top of setup(). Returns the current boot count
 * (persisted in RTC memory across deep sleep, reset to 0 on power loss). */
uint32_t recordBootAndPrintWakeReason();

const char *resetReasonName(esp_reset_reason_t r);

/* §3 sleep sequence: SLPIN -> backlight off -> rail cut -> EXT1 wake ->
 * deep sleep. Does not return. */
[[noreturn]] void enterSleep(TFT_eSPI &tft);

/* §5: dispatch differently depending on which pin woke the device --
 * waking via KEY jumps to the next screen, waking via BOOT just resumes
 * the last one. Only meaningful when the last reset reason was an EXT1
 * wake (see recordBootAndPrintWakeReason()/resetReasonName()). */
enum WakeSource {
    WAKE_SOURCE_NONE,   // not woken by a button (power-on, brownout, etc.)
    WAKE_SOURCE_BOOT,
    WAKE_SOURCE_KEY,
    WAKE_SOURCE_BOTH,   // both pins read low at the moment of wake
};

WakeSource getWakeSource();
