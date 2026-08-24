#pragma once

#include <stdint.h>

/* NimBLE was the original plan (§8), but the proven-working reference on
 * this exact board (T-Display-S3-PC-HW-Monitor) uses the framework's
 * built-in Bluedroid BLE stack instead -- matching that here to avoid
 * another from-scratch BLE bring-up debugging cycle. Revisit NimBLE for
 * power optimization once this path is proven (Milestone 8). */

void initBleService(const char *deviceNamePrefix);

bool bleIsConnected();

/* Bytes received on the last characteristic write, or 0 if none yet. */
uint16_t bleLastWriteLength();
