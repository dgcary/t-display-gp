#pragma once

#include <cstdint>

namespace UiTheme {
constexpr uint16_t BACKGROUND = 0x0000;  // black
constexpr uint16_t TEXT = 0xFFFF;        // white
constexpr uint16_t MUTED = 0xC618;       // light gray
constexpr uint16_t GRID = 0x4208;        // dark gray
constexpr uint16_t POSITIVE = 0xF800;    // A-share up: red
constexpr uint16_t NEGATIVE = 0x07E0;    // A-share down: green
constexpr uint16_t NEUTRAL = MUTED;
constexpr uint16_t CHART = 0x07FF;        // cyan, intentionally sign-neutral
constexpr uint16_t WARNING = 0xFFE0;      // yellow
}  // namespace UiTheme
