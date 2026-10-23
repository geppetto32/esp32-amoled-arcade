#include "sound.h"
#include "config.h"
#include "storage.h"

namespace {
struct Note { uint16_t freq; uint16_t ms; };   // freq 0 = rest; {0,0} terminates
const Note kClick[]    = {{1800, 15}, {0, 0}};
const Note kEat[]      = {{880, 40}, {1320, 50}, {0, 0}};
const Note kLine[]     = {{660, 60}, {880, 60}, {1100, 60}, {1480, 110}, {0, 0}};
const Note kGameOver[] = {{440, 140}, {370, 140}, {294, 140}, {220, 320}, {0, 0}};
const Note kHit[]      = {{520, 25}, {0, 0}};
const Note kShoot[]    = {{1500, 20}, {900, 25}, {0, 0}};
const Note kStart[]    = {{523, 80}, {659, 80}, {784, 80}, {1047, 160}, {0, 0}};
const Note* const kSeq[SFX_COUNT] = {kClick, kEat, kLine, kGameOver, kHit, kShoot, kStart};

const Note* cur = nullptr;
uint32_t noteEnd = 0;
bool muted = false;

void startNote() {
  if (!cur || (cur->freq == 0 && cur->ms == 0)) { noTone(PIN_BUZZER); cur = nullptr; return; }
  if (cur->freq) tone(PIN_BUZZER, cur->freq); else noTone(PIN_BUZZER);
  noteEnd = millis() + cur->ms;
}
}  // namespace

void soundInit() {
  pinMode(PIN_BUZZER, OUTPUT);
  muted = settingMuted();
}

void soundUpdate() {
  if (!cur) return;
  if ((int32_t)(millis() - noteEnd) >= 0) { cur++; startNote(); }
}

void soundPlay(Sfx s) {
  if (muted || s >= SFX_COUNT) return;
  cur = kSeq[s];
  startNote();
}

void soundSetMuted(bool m) {
  muted = m;
  if (m) { cur = nullptr; noTone(PIN_BUZZER); }
}
bool soundMuted() { return muted; }
void soundToggleMute() {
  soundSetMuted(!muted);
  settingSetMuted(muted);
  soundPlay(SFX_CLICK);   // no-op while muted
}
