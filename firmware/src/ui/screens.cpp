#include "screens.h"

#include <Arduino.h>
#include <esp_system.h>

#include "../board_pins.h"
#include "../power/battery.h"
#include "../power/sleep_states.h"
#include "theme.h"

typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
} lcd_cmd_t;

/* ST7789V init sequence from LilyGO factory/tft example (LCD_MODULE_CMD_1) */
static lcd_cmd_t lcd_st7789v[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0X05}, 1},
    {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
    {0xB7, {0X75}, 1},
    {0xBB, {0X28}, 1},
    {0xC0, {0X2C}, 1},
    {0xC2, {0X01}, 1},
    {0xC3, {0X1F}, 1},
    {0xC6, {0X13}, 1},
    {0xD0, {0XA7}, 1},
    {0xD0, {0XA4, 0XA1}, 2},
    {0xD6, {0XA1}, 1},
    {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
    {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},
};

/* AW9364DNR backlight driver: brightness is set by pulse-counting on
 * PIN_LCD_BL, not PWM. level tracks the chip's current state across
 * calls (it has no readback), so this must be the only writer of
 * PIN_LCD_BL once initDisplay() has run. */
static uint8_t s_backlightLevel = 0;

void setBacklightBrightness(uint8_t level)
{
    if (level > BACKLIGHT_MAX_LEVEL) level = BACKLIGHT_MAX_LEVEL;

    if (level == 0) {
        digitalWrite(PIN_LCD_BL, LOW);
        delay(3);
        s_backlightLevel = 0;
        return;
    }
    if (s_backlightLevel == 0) {
        digitalWrite(PIN_LCD_BL, HIGH);
        s_backlightLevel = BACKLIGHT_MAX_LEVEL;
        delayMicroseconds(30);
    }
    int from = BACKLIGHT_MAX_LEVEL - s_backlightLevel;
    int to = BACKLIGHT_MAX_LEVEL - level;
    int pulses = (BACKLIGHT_MAX_LEVEL + to - from) % BACKLIGHT_MAX_LEVEL;
    for (int i = 0; i < pulses; i++) {
        digitalWrite(PIN_LCD_BL, LOW);
        digitalWrite(PIN_LCD_BL, HIGH);
    }
    s_backlightLevel = level;
}

void initDisplay(TFT_eSPI &tft)
{
    tft.begin();
    for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
        tft.writecommand(lcd_st7789v[i].cmd);
        for (int j = 0; j < (lcd_st7789v[i].len & 0x7f); j++) {
            tft.writedata(lcd_st7789v[i].data[j]);
        }
        if (lcd_st7789v[i].len & 0x80) {
            delay(120);
        }
    }

    tft.setRotation(3);

    pinMode(PIN_LCD_BL, OUTPUT);
    s_backlightLevel = 0; /* rail was just cut on sleep, so the chip forgot its level too */
    setBacklightBrightness(DEFAULT_BACKLIGHT_LEVEL);
}

void renderBringupScreen(TFT_eSPI &tft, uint32_t bootCount, float batteryVoltage)
{
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.drawString("TokenSlate M1", 10, 10, 4);
    tft.drawString("display bring-up", 10, 50, 2);

    char hud[48];
    snprintf(hud, sizeof(hud), "boot #%lu  reset: %s",
             (unsigned long)bootCount, resetReasonName(esp_reset_reason()));
    tft.drawString(hud, 10, 80, 2);

    char power[64];
    if (batteryVoltage == 0.0f) {
        snprintf(power, sizeof(power), "battery ADC disabled (known bug)");
    } else if (batteryVoltage > USB_PRESENT_THRESHOLD_V) {
        snprintf(power, sizeof(power), "USB  (adc=%.2fV)", batteryVoltage);
    } else {
        uint8_t pct = batteryVoltageToPercent(batteryVoltage);
        snprintf(power, sizeof(power), "BATT %u%%  (%.2fV)", pct, batteryVoltage);
    }
    tft.drawString(power, 10, 105, 2);
}

static uint16_t statusColor(ConnectionStatus status)
{
    switch (status) {
        case STATUS_LIVE:  return COLOR_STATUS_LIVE;
        case STATUS_STALE: return COLOR_STATUS_STALE;
        case STATUS_ERROR: return COLOR_STATUS_ERROR;
    }
    return COLOR_DIM;
}

static const char *statusLabel(ConnectionStatus status)
{
    switch (status) {
        case STATUS_LIVE:  return "live";
        case STATUS_STALE: return "stale";
        case STATUS_ERROR: return "error";
    }
    return "?";
}

static void drawTopBar(TFT_eSPI &tft, const ProviderCardData &data, float batteryVoltage,
                        uint16_t accent)
{
    tft.fillCircle(10, 9, 5, accent);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(data.name, 22, 2, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(statusColor(data.status), COLOR_BG);
    tft.drawString(statusLabel(data.status), 310, 2, 2);

    char power[24];
    if (batteryVoltage > USB_PRESENT_THRESHOLD_V) {
        snprintf(power, sizeof(power), "USB");
    } else if (batteryVoltage > 0.0f) {
        snprintf(power, sizeof(power), "BATT %u%%", batteryVoltageToPercent(batteryVoltage));
    } else {
        snprintf(power, sizeof(power), "--");
    }
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.drawString(power, 160, 2, 2);
    tft.setTextDatum(TL_DATUM);
}

static void drawSessionRing(TFT_eSPI &tft, uint8_t usedPercent, uint16_t accent)
{
    const int cx = 82, cy = 98, r = 52, ir = 40;
    uint32_t sweep = (uint32_t)usedPercent * 360 / 100;

    tft.drawArc(cx, cy, r, ir, 0, 360, COLOR_RING_TRACK, COLOR_BG, true);
    if (sweep > 0) {
        tft.drawArc(cx, cy, r, ir, 0, sweep, accent, COLOR_BG, true);
    }

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", usedPercent);
    tft.drawString(pct, cx, cy - 6, 4);
    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.drawString("session", cx, cy + 18, 2);
    tft.setTextDatum(TL_DATUM);
}

static void drawWeeklyBar(TFT_eSPI &tft, const ProviderCardData &data, uint16_t accent)
{
    const int x = 175, y = 70, w = 130, h = 18;

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.drawString("weekly", x, y - 16, 2);

    tft.fillRoundRect(x, y, w, h, 4, COLOR_BAR_TRACK);
    int fillW = (int)((uint32_t)w * data.weeklyUsedPercent / 100);
    if (fillW > 0) {
        tft.fillRoundRect(x, y, fillW, h, 4, accent);
    }

    char pct[8];
    snprintf(pct, sizeof(pct), "%u%%", data.weeklyUsedPercent);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(pct, x, y + h + 4, 2);

    tft.setTextColor(COLOR_DIM, COLOR_BG);
    tft.drawString(data.weeklyResetText, x, y + h + 24, 1);
}

static void drawPageDots(TFT_eSPI &tft, uint8_t pageIndex, uint8_t pageCount)
{
    if (pageCount <= 1) return;
    const int dotR = 3, gap = 12;
    int totalW = (pageCount - 1) * gap;
    int startX = (320 - totalW) / 2;
    int y = 162;
    for (uint8_t i = 0; i < pageCount; i++) {
        int x = startX + i * gap;
        if (i == pageIndex) {
            tft.fillCircle(x, y, dotR, COLOR_TEXT);
        } else {
            tft.fillCircle(x, y, dotR, COLOR_DIM);
        }
    }
}

void renderProviderCard(TFT_eSPI &tft, const ProviderCardData &data, float batteryVoltage)
{
    uint16_t accent = providerColor(data.id);

    tft.fillScreen(COLOR_BG);
    drawTopBar(tft, data, batteryVoltage, accent);
    drawSessionRing(tft, data.sessionUsedPercent, accent);
    drawWeeklyBar(tft, data, accent);
    drawPageDots(tft, data.pageIndex, data.pageCount);
}
