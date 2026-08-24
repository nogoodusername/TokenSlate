#pragma once

#include <TFT_eSPI.h>
#include <esp_system.h>

/* Call once at the top of setup(). Returns the current boot count
 * (persisted in RTC memory across deep sleep, reset to 0 on power loss). */
uint32_t recordBootAndPrintWakeReason();

const char *resetReasonName(esp_reset_reason_t r);

/* §3 sleep sequence: SLPIN -> backlight off -> rail cut -> EXT1 wake ->
 * deep sleep. Does not return. */
[[noreturn]] void enterSleep(TFT_eSPI &tft);
