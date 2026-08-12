#pragma once

#include <cstdint>

enum class ButtonEvent { NONE, PREVIOUS, NEXT };

class ButtonDebouncer {
 public:
  explicit ButtonDebouncer(uint32_t debounceMs) : debounceMs_(debounceMs) {}

  // levelHigh is the raw INPUT_PULLUP level. Returns true once per stable HIGH->LOW edge.
  bool update(bool levelHigh, uint32_t nowMs) {
    if (levelHigh != rawHigh_) {
      rawHigh_ = levelHigh;
      rawChangedAtMs_ = nowMs;
    }
    if (rawHigh_ != stableHigh_ && static_cast<uint32_t>(nowMs - rawChangedAtMs_) >= debounceMs_) {
      const bool wasHigh = stableHigh_;
      stableHigh_ = rawHigh_;
      return wasHigh && !stableHigh_;
    }
    return false;
  }

 private:
  uint32_t debounceMs_;
  bool rawHigh_ = true;
  bool stableHigh_ = true;
  uint32_t rawChangedAtMs_ = 0;
};
