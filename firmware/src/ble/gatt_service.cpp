#include "gatt_service.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

/* Placeholder UUIDs from docs §7 -- replace with generated ones before
 * shipping. */
#define SERVICE_UUID          "7a2a0001-6b5f-4a9e-9c9d-1f2e3a4b5c6d"
#define SNAPSHOT_CHAR_UUID    "7a2a0002-6b5f-4a9e-9c9d-1f2e3a4b5c6d"

static BLEServer *s_server = nullptr;
static BLECharacteristic *s_snapshotChar = nullptr;
static bool s_connected = false;
static uint16_t s_lastWriteLength = 0;

static void logLine(const char *fmt, ...)
{
    if (!Serial) return;
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.println(buf);
}

class TokenSlateServerCallbacks : public BLEServerCallbacks
{
    void onConnect(BLEServer *server) override
    {
        s_connected = true;
        logLine("[TokenSlate] BLE central connected");
    }

    void onDisconnect(BLEServer *server) override
    {
        s_connected = false;
        logLine("[TokenSlate] BLE central disconnected, re-advertising");
        delay(200);
        BLEDevice::startAdvertising();
    }
};

class SnapshotWriteCallbacks : public BLECharacteristicCallbacks
{
    void onWrite(BLECharacteristic *characteristic) override
    {
        std::string value = characteristic->getValue();
        s_lastWriteLength = (uint16_t)value.length();
        logLine("[TokenSlate] snapshot write: %u bytes", (unsigned)value.length());
    }
};

void initBleService(const char *deviceNamePrefix)
{
    BLEDevice::init(deviceNamePrefix);
    /* MTU up to 247 per docs §7 (usable payload ~240B after ATT overhead). */
    BLEDevice::setMTU(247);

    s_server = BLEDevice::createServer();
    s_server->setCallbacks(new TokenSlateServerCallbacks());

    BLEService *service = s_server->createService(SERVICE_UUID);
    s_snapshotChar = service->createCharacteristic(
        SNAPSHOT_CHAR_UUID,
        BLECharacteristic::PROPERTY_WRITE); /* Write Request, not WRITE_NR -- see docs §7 */
    s_snapshotChar->setCallbacks(new SnapshotWriteCallbacks());

    service->start();

    BLEAdvertising *advertising = s_server->getAdvertising();
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    logLine("[TokenSlate] BLE advertising as \"%s\"", deviceNamePrefix);
}

bool bleIsConnected()
{
    return s_connected;
}

uint16_t bleLastWriteLength()
{
    return s_lastWriteLength;
}
