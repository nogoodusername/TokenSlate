#pragma once

#include "ui/screens.h"

#define MAX_STORED_PROVIDERS 8

/* RTC_DATA_ATTR-backed provider snapshot + screen index (docs §8) --
 * survives deep sleep so the last-known screen can be redrawn instantly
 * on wake, before any fresh BLE sync completes. Lost only on a real
 * power-on reset (battery disconnected, brownout, first flash). */

ProviderCardData *storageProviders();
char (*storageResetTextBuf())[24];

uint8_t storageProviderCount();
void storageSetProviderCount(uint8_t count);

uint8_t storageCurrentPage();
void storageSetCurrentPage(uint8_t page);

bool storageHaveData();
void storageSetHaveData(bool haveData);
