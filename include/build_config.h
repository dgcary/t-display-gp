#pragma once

#include <cstddef>
#include <cstdint>

namespace BuildConfig {
constexpr uint8_t PIN_POWER = 15;
constexpr uint8_t PIN_BUTTON_PREV = 0;
constexpr uint8_t PIN_BUTTON_NEXT = 14;
constexpr uint32_t DEFAULT_QUOTE_REFRESH_MS = 5000;
constexpr uint32_t MIN_QUOTE_REFRESH_MS = 3000;
constexpr uint32_t MAX_QUOTE_REFRESH_MS = 5000;
constexpr uint32_t INTRADAY_REFRESH_MS = 60000;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 40;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 1500;
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 2500;
constexpr size_t HTTP_MAX_BODY_BYTES = 32768;
constexpr char CONFIG_NAMESPACE[] = "stockticker";
constexpr uint32_t CONFIG_SCHEMA_VERSION = 1;
}  // namespace BuildConfig
