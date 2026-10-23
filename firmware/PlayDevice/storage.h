// storage.h - high scores and settings in NVS (Preferences).
#pragma once
#include <stdint.h>

void     storageInit();
uint32_t hiscoreGet(const char* key);
bool     hiscoreSubmit(const char* key, uint32_t score);   // true if it is a new record
bool     settingMuted();
void     settingSetMuted(bool m);
