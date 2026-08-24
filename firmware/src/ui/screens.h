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
