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

// Shared information-terminal accents. These intentionally avoid changing
// the A-share red-up/green-down semantics above.
constexpr uint16_t ACCENT = 0x07FF;       // cyan
constexpr uint16_t UP = 0x07E0;           // healthy/online green
constexpr uint16_t CARD = 0x18E3;         // near-black card surface
constexpr uint16_t WEATHER_SUN = 0xFFE0;  // yellow
constexpr uint16_t WEATHER_WARM = 0xFD20; // orange
constexpr uint16_t WEATHER_COOL = 0x5D9F; // light sky blue
constexpr uint16_t WEATHER_RAIN = 0x4D7C; // rain blue
constexpr uint16_t WEATHER_FOG = 0xAD55;  // fog gray
constexpr uint16_t CAT = 0xFD20;          // orange tabby
constexpr uint16_t CAT_LIGHT = 0xFED0;    // cream muzzle
constexpr uint16_t CAT_PINK = 0xF81F;     // nose/tongue
}  // namespace UiTheme
