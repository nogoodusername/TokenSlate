#include "storage.h"

RTC_DATA_ATTR static ProviderCardData s_providers[MAX_STORED_PROVIDERS];
RTC_DATA_ATTR static char s_resetTextBuf[MAX_STORED_PROVIDERS][24];
RTC_DATA_ATTR static uint8_t s_providerCount = 0;
RTC_DATA_ATTR static uint8_t s_currentPage = 0;
RTC_DATA_ATTR static bool s_haveData = false;

ProviderCardData *storageProviders() { return s_providers; }
char (*storageResetTextBuf())[24] { return s_resetTextBuf; }

uint8_t storageProviderCount() { return s_providerCount; }
void storageSetProviderCount(uint8_t count) { s_providerCount = count; }

uint8_t storageCurrentPage() { return s_currentPage; }
void storageSetCurrentPage(uint8_t page) { s_currentPage = page; }

bool storageHaveData() { return s_haveData; }
void storageSetHaveData(bool haveData) { s_haveData = haveData; }
