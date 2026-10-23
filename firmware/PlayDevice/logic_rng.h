// logic_rng.h - tiny deterministic RNG for display-free game logic (host-testable).
#pragma once
#include <stdint.h>

inline uint32_t rngNext(uint32_t& s) {
  if (s == 0) s = 0x9E3779B9u;
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
