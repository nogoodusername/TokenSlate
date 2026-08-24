#pragma once

#include <TFT_eSPI.h>

/* ST7789V init sequence (LCD_MODULE_CMD_1) + rotation/backlight on.
 * Mirrors Xinyuan-LilyGO/T-Display-S3's tft.ino, proven working on
 * this board (see Milestone 0). */
void initDisplay(TFT_eSPI &tft);

/* Milestone 1 demo: status text + boot/reset HUD + battery/USB readout.
 * batteryVoltage must be sampled by the caller *before* initDisplay() --
 * reading GPIO4 while the LCD's parallel bus is active can hang, see
 * docs §3 / firmware/src/power/battery.cpp. */
void renderBringupScreen(TFT_eSPI &tft, uint32_t bootCount, float batteryVoltage);

/* Milestone 4: provider card. §8 layout -- colored dot + name top-left,
 * connection/last-sync status + battery/USB icon top-right, radial ring
 * for the primary (session) window with numeric % in the center, a
 * horizontal bar for the secondary (weekly) window with reset countdown
 * text below it, page dots at the bottom. Against hardcoded data for
 * this milestone -- wired to real BLE data in Milestone 5. */
enum ConnectionStatus {
    STATUS_LIVE,
    STATUS_STALE,
    STATUS_ERROR,
};

struct ProviderCardData {
    const char *id;
    const char *name;
    uint8_t sessionUsedPercent;   // primary window
    uint8_t weeklyUsedPercent;    // secondary window
    const char *weeklyResetText;  // e.g. "resets in 2d 4h"
    ConnectionStatus status;
    uint8_t pageIndex;   // 0-based
    uint8_t pageCount;
};

void renderProviderCard(TFT_eSPI &tft, const ProviderCardData &data, float batteryVoltage);
