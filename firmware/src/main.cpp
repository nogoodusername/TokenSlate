#include <Arduino.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <string.h>

#include "ble/gatt_service.h"
#include "board_pins.h"
#include "power/battery.h"
#include "power/sleep_states.h"
#include "storage.h"
#include "ui/screens.h"
#include "ui/theme.h"

/* ------------------------------------------------------------------ */
/*  TokenSlate — Milestone 6: power states & buttons                   */
/*  Idle timeout, BOOT sleep-now, KEY short/long press, EXT1           */
/*  wake-source dispatch, RTC-memory persistence across sleep (§5/§6). */
/* ------------------------------------------------------------------ */

#define DEVICE_NAME "TokenSlate-"

#define IDLE_TIMEOUT_MS 25000UL   // §12: 20-30s default
#define LONG_PRESS_MS   500UL     // §6: "held ~300-500ms after the wake ISR runs"
#define BATTERY_RESAMPLE_MS 5000UL

TFT_eSPI tft = TFT_eSPI();

static ProviderCardData *providers;
static char (*resetTextBuf)[24];
static unsigned long lastActivity = 0;
static float batteryVoltage = 0.0f;

static void resetIdleTimer()
{
    lastActivity = millis();
}

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
    if (!storageHaveData()) return;
    uint8_t page = storageCurrentPage();
    providers[page].pageIndex = page;
    renderProviderCard(tft, providers[page], batteryVoltage);
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
        if (count >= MAX_STORED_PROVIDERS) break;

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

    storageSetProviderCount(count);
    for (uint8_t i = 0; i < count; i++) {
        providers[i].pageCount = count;
    }
    if (storageCurrentPage() >= count) storageSetCurrentPage(0);
    storageSetHaveData(true);

    if (Serial) Serial.printf("[TokenSlate] snapshot applied: %u providers\n", count);
    renderCurrentPage();
}

/* KEY: short press = next screen, long press = force a resync now (§6).
 * Blocks until release, which is fine -- the debounce/hold-measurement
 * loop is the only thing happening while a button is actually held. */
static void handleKeyPress()
{
    delay(30); /* debounce */
    if (digitalRead(PIN_BUTTON_2) != LOW) return; /* was noise */

    unsigned long pressStart = millis();
    while (digitalRead(PIN_BUTTON_2) == LOW) delay(10);
    unsigned long heldMs = millis() - pressStart;

    if (heldMs >= LONG_PRESS_MS) {
        bleForceReadvertise();
    } else if (storageHaveData()) {
        uint8_t next = (storageCurrentPage() + 1) % storageProviderCount();
        storageSetCurrentPage(next);
        renderCurrentPage();
    }
    resetIdleTimer();
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
    WakeSource wakeSource = getWakeSource();

    providers = storageProviders();
    resetTextBuf = storageResetTextBuf();

    initDisplay(tft);

    if (storageHaveData()) {
        /* §5: waking via KEY jumps to the next screen; BOOT (or a plain
         * power-on) just resumes whatever was last shown. Renders from
         * the RTC-persisted cache instantly, before any fresh BLE write
         * arrives. */
        if (wakeSource == WAKE_SOURCE_KEY) {
            uint8_t next = (storageCurrentPage() + 1) % storageProviderCount();
            storageSetCurrentPage(next);
        }
        renderCurrentPage();
    } else {
        renderWaitingScreen();
    }

    initBleService(DEVICE_NAME);
    resetIdleTimer();
}

void loop()
{
    if (bleHasNewSnapshot()) {
        char json[BLE_SNAPSHOT_MAX_LEN];
        if (bleConsumeSnapshotJson(json, sizeof(json))) {
            handleSnapshot(json);
        }
    }

    /* Battery/USB was previously only sampled once at boot, so the
     * indicator went stale as soon as the power source changed (e.g.
     * unplugging USB) without a reboot. Re-sample periodically instead. */
    static unsigned long lastBatterySample = 0;
    if (millis() - lastBatterySample >= BATTERY_RESAMPLE_MS) {
        lastBatterySample = millis();
        batteryVoltage = readBatteryVoltage();
        renderCurrentPage();
    }

    if (digitalRead(PIN_BUTTON_2) == LOW) {
        handleKeyPress();
    }

    /* BOOT press = sleep now, per §6. */
    if (digitalRead(PIN_BUTTON_1) == LOW) {
        delay(30); /* debounce */
        if (digitalRead(PIN_BUTTON_1) == LOW) {
            enterSleep(tft);
        }
    }

    if (millis() - lastActivity >= IDLE_TIMEOUT_MS) {
        enterSleep(tft);
    }

    delay(50);
}
