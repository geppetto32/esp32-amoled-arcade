#include "storage.h"
#include <Preferences.h>

static Preferences prefs;
static bool ready = false;

void storageInit() { ready = prefs.begin("playdev", false); }

uint32_t hiscoreGet(const char* key) { return ready ? prefs.getUInt(key, 0) : 0; }

bool hiscoreSubmit(const char* key, uint32_t score) {
  if (score == 0 || score <= hiscoreGet(key)) return false;
  if (ready) prefs.putUInt(key, score);
  return true;
}

bool settingMuted() { return ready ? prefs.getBool("mute", false) : false; }
void settingSetMuted(bool m) { if (ready) prefs.putBool("mute", m); }
