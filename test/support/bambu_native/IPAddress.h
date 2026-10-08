#pragma once
#include "Arduino.h"
class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint8_t, uint8_t, uint8_t, uint8_t) {}
  explicit IPAddress(const uint8_t*) {}
};
