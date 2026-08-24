#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_sleep.h>
#include <esp_system.h>

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 0: power sanity check                       */
/*  Display init mirrors Xinyuan-LilyGO/T-Display-S3's tft.ino         */
/*  (proven working on this board). Sleep sequence follows §3:         */
/*  SLPIN -> backlight off -> rail cut -> EXT1 wake -> deep sleep.     */
/* ------------------------------------------------------------------ */

#define PIN_POWER_ON 15
#define PIN_LCD_BL   38
#define PIN_BUTTON_1 0   // BOOT
#define PIN_BUTTON_2 14  // KEY

#define AWAKE_MS 10000UL

TFT_eSPI tft = TFT_eSPI();

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

RTC_DATA_ATTR static uint32_t boot_count = 0;

static const char *reset_reason_name(esp_reset_reason_t r)
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

static void print_wake_reason()
{
    boot_count++;
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    esp_reset_reason_t reason = esp_reset_reason();
    /* Serial (native USB-CDC) blocks on write/flush when no host has the
     * port open (e.g. battery-only, no cable) — guard every call with
     * `if (Serial)` or the whole sketch hangs here. */
    if (Serial) {
        Serial.printf("[TokenSlate] boot #%lu, reset reason: %s, wake cause: %d\n",
                      (unsigned long)boot_count, reset_reason_name(reason), (int)cause);
        if (cause == ESP_SLEEP_WAKEUP_EXT1) {
            uint64_t mask = esp_sleep_get_ext1_wakeup_status();
            Serial.printf("[TokenSlate] EXT1 wake mask: 0x%llx\n", mask);
        }
    }
}

static void go_to_sleep()
{
    if (Serial) {
        Serial.println("[TokenSlate] entering sleep sequence (SLPIN -> rail cut -> deep sleep)");
        Serial.flush();
    }

    tft.writecommand(0x10); /* SLPIN */
    delay(120);

    digitalWrite(PIN_LCD_BL, LOW);
    digitalWrite(PIN_POWER_ON, LOW);

    /* Both buttons are active-low (pulled up, pressed = LOW). ANY_HIGH
     * would wake immediately since GPIO0 idles HIGH; per §3, use ANY_LOW
     * so either button wakes the device. */
    esp_sleep_enable_ext1_wakeup(
        (1ULL << PIN_BUTTON_1) | (1ULL << PIN_BUTTON_2),
        ESP_EXT1_WAKEUP_ANY_LOW);

    esp_deep_sleep_start();
}

void setup()
{
    Serial.begin(115200);
    delay(50);

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);

    print_wake_reason();

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
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.drawString("TokenSlate M0", 10, 10, 4);
    tft.drawString("power sanity check", 10, 50, 2);

    char hud[48];
    snprintf(hud, sizeof(hud), "boot #%lu  reset: %s",
             (unsigned long)boot_count, reset_reason_name(esp_reset_reason()));
    tft.drawString(hud, 10, 80, 2);

    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    if (Serial) {
        Serial.println("[TokenSlate] awake, sleeping in 10s");
    }
}

void loop()
{
    static unsigned long start = millis();
    if (millis() - start >= AWAKE_MS) {
        go_to_sleep();
    }
    delay(50);
}
