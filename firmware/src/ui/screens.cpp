#include "screens.h"

#include <Arduino.h>
#include <esp_system.h>

#include "../board_pins.h"
#include "../power/battery.h"
#include "../power/sleep_states.h"

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
    digitalWrite(PIN_LCD_BL, HIGH);
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
