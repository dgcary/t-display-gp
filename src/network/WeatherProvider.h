#pragma once

#include <cstddef>
#include <cstdint>

#include "AppConfig.h"
#include "HttpTransport.h"

enum class WeatherError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  BODY_TOO_LARGE,
  PARSE,
  MISSING_FIELD,
};

struct WeatherDiagnostics {
  int httpStatus = 0;
  int nativeError = 0;
  int tlsError = 0;
  int32_t expectedBytes = -1;
  size_t receivedBytes = 0;
  uint32_t elapsedMs = 0;
};

struct DailyForecast {
  float highTemp = 0.0f;
  float lowTemp = 0.0f;
  int weatherCode = 0;
};

struct WeatherSnapshot {
  float currentTemp = 0.0f;
  float apparentTemp = 0.0f;
  int humidityPercent = 0;
  float windSpeed = 0.0f;
  int precipitationProbabilityPercent = 0;
  int weatherCode = 0;
  DailyForecast today;
  DailyForecast tomorrow;
  DailyForecast dayAfter;
  uint64_t updatedEpochSeconds = 0;
};

class IWeatherProvider {
 public:
  virtual ~IWeatherProvider() = default;
  virtual WeatherError fetch(const LocationConfig& location, WeatherSnapshot& out,
                             WeatherDiagnostics* diagnostics = nullptr) = 0;
};

class OpenMeteoProvider final : public IWeatherProvider {
 public:
  explicit OpenMeteoProvider(IHttpTransport& transport) : transport_(transport) {}
  WeatherError fetch(const LocationConfig& location, WeatherSnapshot& out,
                     WeatherDiagnostics* diagnostics = nullptr) override;

 private:
  IHttpTransport& transport_;
};
