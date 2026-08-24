#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_system.h>

#include "ble/gatt_service.h"
#include "board_pins.h"
#include "power/sleep_states.h"
#include "ui/screens.h"

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 2: BLE peripheral skeleton                  */
/*  Advertises the TokenSlate service + snapshot characteristic (§7).  */
/*  Stays awake indefinitely (no idle timeout) so it can be found and  */
/*  connected to with a generic BLE scanner (e.g. nRF Connect); BOOT   */
/*  still sleeps immediately, per §6.                                  */
/* ------------------------------------------------------------------ */

#define DEVICE_NAME "TokenSlate-"

TFT_eSPI tft = TFT_eSPI();

static void renderBleStatus(bool connected, uint16_t lastWriteLen)
{
    tft.fillRect(0, 105, 320, 65, TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);

    char line1[48];
    snprintf(line1, sizeof(line1), "BLE: %s", connected ? "connected" : "advertising");
    tft.drawString(line1, 10, 105, 2);

    char line2[48];
    snprintf(line2, sizeof(line2), "last write: %u bytes", (unsigned)lastWriteLen);
    tft.drawString(line2, 10, 130, 2);
}

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(50);

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);

    uint32_t bootCount = recordBootAndPrintWakeReason();

    initDisplay(tft);
    tft.fillScreen(TFT_BLUE);
    tft.setTextColor(TFT_WHITE, TFT_BLUE);
    tft.drawString("TokenSlate M2", 10, 10, 4);
    tft.drawString("BLE peripheral", 10, 50, 2);

    char hud[48];
    snprintf(hud, sizeof(hud), "boot #%lu  reset: %s",
             (unsigned long)bootCount, resetReasonName(esp_reset_reason()));
    tft.drawString(hud, 10, 80, 2);

    initBleService(DEVICE_NAME);
    renderBleStatus(false, 0);
}

void loop()
{
    static bool lastConnected = false;
    static uint16_t lastWriteLen = 0;

    bool connected = bleIsConnected();
    uint16_t writeLen = bleLastWriteLength();
    if (connected != lastConnected || writeLen != lastWriteLen) {
        lastConnected = connected;
        lastWriteLen = writeLen;
        renderBleStatus(connected, writeLen);
    }

    /* BOOT press = sleep now, per §6. */
    if (digitalRead(PIN_BUTTON_1) == LOW) {
        delay(30); /* debounce */
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            enterSleep(tft);
        }
    }

    delay(50);
}
