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
constexpr uint32_t BUTTON_LONG_PRESS_MS = 700;
constexpr uint32_t WEATHER_DEFAULT_REFRESH_MIN = 15;
constexpr uint32_t HTTP_CONNECT_TIMEOUT_MS = 1500;
constexpr uint32_t HTTP_READ_TIMEOUT_MS = 2500;
constexpr uint32_t HTTP_TLS_HANDSHAKE_TIMEOUT_SEC = 5;
constexpr size_t HTTP_MAX_BODY_BYTES = 32768;
constexpr size_t BAMBU_HTTPS_MAX_BODY_BYTES = 16384;
constexpr uint32_t CURRENT_QUOTE_REQUEST_TTL_MS = 8000;
constexpr uint32_t BACKGROUND_QUOTE_REQUEST_TTL_MS = 12000;
constexpr uint32_t PRIMARY_PROBE_REQUEST_TTL_MS = 30000;
constexpr uint32_t INTRADAY_REQUEST_TTL_MS = 75000;
constexpr uint32_t INTRADAY_RETRY_CYCLE_TTL_MS = 15000;
constexpr uint8_t INTRADAY_MAX_ATTEMPTS = 3;
constexpr uint32_t INTRADAY_RETRY_1_MS = 1500;
constexpr uint32_t INTRADAY_RETRY_2_MS = 4000;
constexpr uint32_t QUOTE_DELAY_MS = 15000;
constexpr uint32_t INTRADAY_DELAY_MS = 180000;
constexpr uint32_t CHANNEL_DELAY_FAILURE_CYCLES = 2;
constexpr char CONFIG_NAMESPACE[] = "stockticker";
constexpr uint32_t CONFIG_SCHEMA_VERSION = 2;
constexpr char BAMBU_CONFIG_NAMESPACE[] = "bambucloud";
constexpr char BAMBU_CONFIG_KEY[] = "config";
}  // namespace BuildConfig
