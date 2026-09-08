#pragma once

#include <cstdint>

enum class ButtonGesture {
  NONE,
  SHORT_PRESS,
  LONG_PRESS,
};

enum class InputEvent {
  NONE,
  PREV_SHORT,
  NEXT_SHORT,
  PREV_LONG,
  NEXT_LONG,
};

class ButtonInput {
 public:
  ButtonInput(uint32_t debounceMs, uint32_t longPressMs)
      : debounceMs_(debounceMs), longPressMs_(longPressMs) {}

  ButtonGesture update(bool levelHigh, uint32_t nowMs) {
    if (levelHigh != rawHigh_) {
      rawHigh_ = levelHigh;
      rawChangedAtMs_ = nowMs;
    }

    ButtonGesture event = ButtonGesture::NONE;
    if (rawHigh_ != stableHigh_ && elapsed(nowMs, rawChangedAtMs_) >= debounceMs_) {
      const bool wasHigh = stableHigh_;
      stableHigh_ = rawHigh_;
      if (wasHigh && !stableHigh_) {
        pressedAtMs_ = rawChangedAtMs_;
        longEmitted_ = false;
      } else if (!wasHigh && stableHigh_) {
        if (!longEmitted_) event = ButtonGesture::SHORT_PRESS;
        longEmitted_ = false;
      }
    }

    if (!rawHigh_ && !stableHigh_ && !longEmitted_ &&
        elapsed(nowMs, pressedAtMs_) >= longPressMs_) {
      longEmitted_ = true;
      return ButtonGesture::LONG_PRESS;
    }
    return event;
  }

 private:
  static uint32_t elapsed(uint32_t nowMs, uint32_t thenMs) {
    return static_cast<uint32_t>(nowMs - thenMs);
  }

  uint32_t debounceMs_;
  uint32_t longPressMs_;
  bool rawHigh_ = true;
  bool stableHigh_ = true;
  bool longEmitted_ = false;
  uint32_t rawChangedAtMs_ = 0;
  uint32_t pressedAtMs_ = 0;
};
