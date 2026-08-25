#pragma once

#include <cstddef>
#include <cstdint>

namespace BadApplePlayback {
constexpr uint16_t WIDTH = 168;
constexpr uint16_t HEIGHT = 126;
constexpr uint16_t ROW_BYTES = WIDTH / 8;
constexpr size_t FRAME_BYTES = static_cast<size_t>(ROW_BYTES) * HEIGHT;
constexpr uint32_t FPS = 10;
constexpr uint32_t FRAME_INTERVAL_MS = 1000U / FPS;
constexpr uint32_t FRAME_COUNT = 2190U;
constexpr uint32_t LOOP_DURATION_MS = FRAME_COUNT * FRAME_INTERVAL_MS;

uint32_t frameIndex(uint32_t elapsedMs);
bool applyDelta(uint8_t* frame, size_t frameSize, const uint8_t* encoded, size_t encodedSize);
}  // namespace BadApplePlayback
