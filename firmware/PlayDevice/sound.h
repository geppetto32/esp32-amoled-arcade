// sound.h - non-blocking beep queue on the passive piezo.
#pragma once
#include <stdint.h>

enum Sfx : uint8_t { SFX_CLICK, SFX_EAT, SFX_LINE, SFX_GAMEOVER, SFX_HIT, SFX_SHOOT, SFX_START, SFX_COUNT };

void soundInit();
void soundUpdate();          // call every loop iteration
void soundPlay(Sfx s);       // starts the effect (replaces whatever is playing)
void soundSetMuted(bool m);
bool soundMuted();
void soundToggleMute();      // persists the setting
