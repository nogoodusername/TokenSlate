#include <Arduino.h>
#include <TFT_eSPI.h>

#include "board_pins.h"
#include "power/battery.h"
#include "power/sleep_states.h"
#include "ui/screens.h"

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 4: screen UI                                 */
/*  Provider card layout (§8) against hardcoded placeholder data --     */
/*  wired to real BLE data in Milestone 5. KEY cycles providers, BOOT   */
/*  sleeps immediately, per §6. Stays awake indefinitely otherwise so   */
/*  the UI can be inspected at leisure -- idle-timeout auto-sleep is    */
/*  Milestone 6's concern.                                              */
/* ------------------------------------------------------------------ */

TFT_eSPI tft = TFT_eSPI();

static ProviderCardData providers[] = {
    {"codex",  "Codex",  28, 41, "resets in 3h 12m",  STATUS_LIVE,  0, 3},
    {"claude", "Claude", 92, 63, "resets in 1d 4h",   STATUS_STALE, 1, 3},
    {"cursor", "Cursor", 5,  97, "resets in 6d 20h",  STATUS_ERROR, 2, 3},
};
static const uint8_t providerCount = sizeof(providers) / sizeof(providers[0]);
static uint8_t currentPage = 0;
static float batteryVoltage = 0.0f;

static void renderCurrentPage()
{
    providers[currentPage].pageIndex = currentPage;
    renderProviderCard(tft, providers[currentPage], batteryVoltage);
}

void setup()
{
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);
    delay(50);

    /* Sample the battery ADC before touching the LCD bus at all -- see
     * docs §3 / power/battery.cpp. */
    batteryVoltage = readBatteryVoltage();

    pinMode(PIN_POWER_ON, OUTPUT);
    digitalWrite(PIN_POWER_ON, HIGH);
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);

    recordBootAndPrintWakeReason();

    initDisplay(tft);
    renderCurrentPage();
}

void loop()
{
    /* KEY short press = next provider screen, per §6. */
    if (digitalRead(PIN_BUTTON_2) == LOW) {
        delay(30); /* debounce */
        if (digitalRead(PIN_BUTTON_2) == LOW) {
            currentPage = (currentPage + 1) % providerCount;
            renderCurrentPage();
            while (digitalRead(PIN_BUTTON_2) == LOW) delay(10); /* wait for release */
        }
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
