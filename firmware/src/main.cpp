#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <string.h>

#include "ble/gatt_service.h"
#include "board_pins.h"
#include "power/battery.h"
#include "power/sleep_states.h"
#include "ui/screens.h"
#include "ui/theme.h"

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 5: wire it live                             */
/*  Parses the bridge's real BLE snapshot writes (docs §7) and renders */
/*  real provider cards (Milestone 4's UI). KEY cycles providers,      */
/*  BOOT sleeps immediately (§6). No idle-timeout auto-sleep or        */
/*  RTC-memory persistence yet -- that's Milestone 6.                  */
/* ------------------------------------------------------------------ */

#define DEVICE_NAME "TokenSlate-"
#define MAX_PROVIDERS 8

TFT_eSPI tft = TFT_eSPI();

static ProviderCardData providers[MAX_PROVIDERS];
static char resetTextBuf[MAX_PROVIDERS][24];
static uint8_t providerCount = 0;
static uint8_t currentPage = 0;
static bool haveData = false;
static float batteryVoltage = 0.0f;

static void formatCountdown(char *buf, size_t bufLen, long secondsRemaining)
{
    if (secondsRemaining <= 0) {
        snprintf(buf, bufLen, "resets soon");
        return;
    }
    long days = secondsRemaining / 86400;
    long hours = (secondsRemaining % 86400) / 3600;
    long minutes = (secondsRemaining % 3600) / 60;
    if (days > 0) {
        snprintf(buf, bufLen, "resets in %ldd %ldh", days, hours);
    } else if (hours > 0) {
        snprintf(buf, bufLen, "resets in %ldh %ldm", hours, minutes);
    } else {
        snprintf(buf, bufLen, "resets in %ldm", minutes);
    }
}

static void renderCurrentPage()
{
    if (!haveData) return;
    providers[currentPage].pageIndex = currentPage;
    renderProviderCard(tft, providers[currentPage], batteryVoltage);
}

static void renderWaitingScreen()
{
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("TokenSlate", 10, 10, 4);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("waiting for data from bridge...", 10, 50, 2);
}

/* Parses a docs §7 snapshot: {"v":1,"t":<epoch>,"p":[{"i":...,"p1":...,
 * "p2":...,"r1":...,"r2":...,"s":...}, ...]}. Note: p1/p2 currently only
 * cover the first two of a provider's (possibly >2) windows -- see the
 * known limitation in docs §11 Milestone 3. */
static void handleSnapshot(const char *json)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        if (Serial) Serial.printf("[TokenSlate] snapshot parse failed: %s\n", err.c_str());
        return;
    }

    long snapshotTime = doc["t"] | 0;
    JsonArray arr = doc["p"];
    uint8_t count = 0;

    for (JsonObject p : arr) {
        if (count >= MAX_PROVIDERS) break;

        const char *id = p["i"] | "?";
        strncpy(providers[count].id, id, sizeof(providers[count].id) - 1);
        providers[count].id[sizeof(providers[count].id) - 1] = '\0';

        /* Pass the stable copy, not the transient JSON-parse pointer --
         * providerDisplayName()'s fallback returns its input unchanged,
         * which would otherwise dangle once `doc` goes out of scope. */
        providers[count].name = providerDisplayName(providers[count].id);
        providers[count].sessionUsedPercent = p["p1"] | 0;
        providers[count].weeklyUsedPercent = p["p2"] | 0;

        long r2 = p["r2"] | 0;
        formatCountdown(resetTextBuf[count], sizeof(resetTextBuf[count]),
                        r2 > 0 ? (r2 - snapshotTime) : -1);
        providers[count].weeklyResetText = resetTextBuf[count];

        int status = p["s"] | 0;
        providers[count].status = (status == 2) ? STATUS_ERROR
                                 : (status == 1) ? STATUS_STALE
                                                  : STATUS_LIVE;
        providers[count].pageCount = 0; /* filled in below, once count is known */

        count++;
    }

    if (count == 0) return;

    providerCount = count;
    for (uint8_t i = 0; i < providerCount; i++) {
        providers[i].pageCount = providerCount;
    }
    if (currentPage >= providerCount) currentPage = 0;
    haveData = true;

    if (Serial) Serial.printf("[TokenSlate] snapshot applied: %u providers\n", providerCount);
    renderCurrentPage();
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
    renderWaitingScreen();

    initBleService(DEVICE_NAME);
}

void loop()
{
    if (bleHasNewSnapshot()) {
        char json[BLE_SNAPSHOT_MAX_LEN];
        if (bleConsumeSnapshotJson(json, sizeof(json))) {
            handleSnapshot(json);
        }
    }

    /* KEY short press = next provider screen, per §6. */
    if (digitalRead(PIN_BUTTON_2) == LOW) {
        delay(30); /* debounce */
        if (digitalRead(PIN_BUTTON_2) == LOW && haveData) {
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
