#pragma once

#include <stddef.h>
#include <stdint.h>

/* NimBLE was the original plan (§8), but the proven-working reference on
 * this exact board (T-Display-S3-PC-HW-Monitor) uses the framework's
 * built-in Bluedroid BLE stack instead -- matching that here to avoid
 * another from-scratch BLE bring-up debugging cycle. Revisit NimBLE for
 * power optimization once this path is proven (Milestone 8). */

/* Max size of one snapshot JSON payload. Larger than docs §7's ~240B
 * single-MTU estimate -- Bluedroid reassembles a long characteristic
 * write into one onWrite() call regardless of how many ATT PDUs it
 * took, so the real ceiling is "however many providers are configured",
 * not one MTU. 8 providers x ~80B/provider + overhead comfortably fits
 * in 1024B with headroom. */
#define BLE_SNAPSHOT_MAX_LEN 1024

void initBleService(const char *deviceNamePrefix);

bool bleIsConnected();

/* Bytes received on the last characteristic write, or 0 if none yet. */
uint16_t bleLastWriteLength();

/* True once, after a characteristic write, until consumed. The write
 * callback only copies bytes -- keep JSON parsing out of that interrupt
 * context and do it from the main loop instead (matches the pattern in
 * T-Display-S3-PC-HW-Monitor's ble_server.cpp). */
bool bleHasNewSnapshot();

/* NUL-terminated. Returns false (and leaves buf untouched) if nothing
 * new is waiting, or the payload doesn't fit in bufLen. Clears the
 * "new snapshot" flag either way once called. */
bool bleConsumeSnapshotJson(char *buf, size_t bufLen);

/* KEY long press = force a resync now (§6). The bridge is already
 * scanning continuously while the device is awake (docs §9), so there's
 * no separate "request" characteristic -- this just restarts
 * advertising so the bridge's next scan pass finds the device sooner
 * rather than waiting out whatever's left of the current interval. */
void bleForceReadvertise();
