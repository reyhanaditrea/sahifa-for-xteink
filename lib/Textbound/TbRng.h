#pragma once

#include <cstdint>

namespace tb {

// xorshift32. Small enough to serialize with the run, so a resumed game
// continues the exact sequence the run was generating before the power cut.
struct Rng {
  uint32_t s = 1;

  void seed(uint32_t value) { s = value ? value : 1; }

  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }

  // Unbiased enough for content selection; the modulo skew over a table of a
  // few dozen entries is far below anything a player could notice.
  uint32_t below(uint32_t bound) { return bound == 0 ? 0 : next() % bound; }

  // Inclusive range.
  uint32_t range(uint32_t lo, uint32_t hi) { return hi <= lo ? lo : lo + below(hi - lo + 1); }

  bool chance(uint32_t percent) { return below(100) < percent; }
};

}  // namespace tb
