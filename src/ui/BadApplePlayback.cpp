#include "BadApplePlayback.h"

namespace {
bool readVarUInt(const uint8_t* data, size_t size, size_t& pos, uint32_t& out) {
  out = 0;
  uint8_t shift = 0;
  while (pos < size && shift <= 28) {
    const uint8_t byte = data[pos++];
    out |= static_cast<uint32_t>(byte & 0x7FU) << shift;
    if ((byte & 0x80U) == 0) return true;
    shift = static_cast<uint8_t>(shift + 7U);
  }
  return false;
}
}  // namespace

namespace BadApplePlayback {
uint32_t frameIndex(uint32_t elapsedMs) {
  return (elapsedMs / FRAME_INTERVAL_MS) % FRAME_COUNT;
}

bool applyDelta(uint8_t* frame, size_t frameSize, const uint8_t* encoded, size_t encodedSize) {
  if (!frame || (!encoded && encodedSize != 0)) return false;
  size_t input = 0;
  size_t cursor = 0;
  while (input < encodedSize) {
    uint32_t skip = 0;
    uint32_t literal = 0;
    if (!readVarUInt(encoded, encodedSize, input, skip)) return false;
    if (skip > frameSize - cursor) return false;
    cursor += static_cast<size_t>(skip);
    if (!readVarUInt(encoded, encodedSize, input, literal)) return false;
    if (literal > frameSize - cursor || literal > encodedSize - input) return false;
    for (uint32_t i = 0; i < literal; ++i) {
      frame[cursor + i] ^= encoded[input + i];
    }
    cursor += static_cast<size_t>(literal);
    input += static_cast<size_t>(literal);
  }
  return true;
}
}  // namespace BadApplePlayback
