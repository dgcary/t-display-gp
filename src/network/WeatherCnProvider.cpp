#include "WeatherCnProvider.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// The mirror does not expose precipitation probability; the provider marks the
// probability unavailable instead of inventing data.
constexpr int kProbabilityUnavailable = 0;

// Approximate wind speed (m/s) per Beaufort level reported by the forecast.
constexpr float kWindLevelSpeed[] = {0.0f, 1.0f, 2.5f, 4.5f, 7.0f, 9.5f, 12.5f,
                                     15.5f, 19.0f, 22.5f, 26.0f, 29.5f, 33.0f};

void copyDiagnostics(const HttpResponse& response, WeatherDiagnostics* diagnostics) {
  if (!diagnostics) return;
  diagnostics->httpStatus = response.statusCode;
  diagnostics->nativeError = response.nativeError;
  diagnostics->tlsError = response.tlsError;
  diagnostics->expectedBytes = response.expectedBytes;
  diagnostics->receivedBytes = response.receivedBytes;
  diagnostics->elapsedMs = response.elapsedMs;
}

WeatherError mapTransportError(HttpTransportError error) {
  switch (error) {
    case HttpTransportError::NONE: return WeatherError::NONE;
    case HttpTransportError::NETWORK: return WeatherError::NETWORK;
    case HttpTransportError::HTTP_STATUS: return WeatherError::HTTP_STATUS;
    case HttpTransportError::BODY_TOO_LARGE: return WeatherError::BODY_TOO_LARGE;
    case HttpTransportError::TRUNCATED_BODY: return WeatherError::NETWORK;
  }
  return WeatherError::NETWORK;
}

bool finiteRange(float value, float minValue, float maxValue) {
  return std::isfinite(value) && value >= minValue && value <= maxValue;
}

// Map a Chinese weather text (UTF-8, e.g. "多云"/"小雨") to the WMO code space
// used by the UI. Heaviest conditions are checked first.
int mapWeatherText(const char* text) {
  if (!text || !*text) return 3;
  const std::string condition(text);
  if (condition.find("冰雹") != std::string::npos) return 99;
  if (condition.find("雷") != std::string::npos) return 95;
  if (condition.find("雨夹雪") != std::string::npos) return 67;
  if (condition.find("冻雨") != std::string::npos) return 66;
  if (condition.find("雪") != std::string::npos) return 73;
  if (condition.find("大暴雨") != std::string::npos) return 82;
  if (condition.find("暴雨") != std::string::npos) return 81;
  if (condition.find("大雨") != std::string::npos) return 65;
  if (condition.find("中雨") != std::string::npos) return 63;
  if (condition.find("阵雨") != std::string::npos) return 80;
  if (condition.find("小雨") != std::string::npos) return 61;
  if (condition.find("雨") != std::string::npos) return 61;
  if (condition.find("雾") != std::string::npos) return 45;
  if (condition.find("霾") != std::string::npos) return 5;
  if (condition.find("沙") != std::string::npos || condition.find("尘") != std::string::npos) return 4;
  if (condition.find("晴") != std::string::npos) return 0;
  if (condition.find("多云") != std::string::npos) return 2;
  if (condition.find("阴") != std::string::npos) return 3;
  return 3;
}

// Parse the first number in a string ("高温 15℃" -> 15, "71%" -> 71).
float parseLeadingNumber(const char* text) {
  if (!text || !*text) return NAN;
  const char* p = text;
  while (*p && (*p < '0' || *p > '9') && *p != '-') ++p;
  if (!*p) return NAN;
  return std::strtof(p, nullptr);
}

// The forecast reports wind level strings ("2级"); approximate a wind speed.
float windSpeedFromLevel(const char* level) {
  const float value = parseLeadingNumber(level);
  if (!std::isfinite(value)) return 0.0f;
  int index = static_cast<int>(value);
  if (index < 0) index = 0;
  if (index > 12) index = 12;
  return kWindLevelSpeed[index];
}

DailyForecast parseForecastDay(JsonObjectConst day) {
  DailyForecast forecast;
  forecast.highTemp = parseLeadingNumber(day["high"].as<const char*>());
  forecast.lowTemp = parseLeadingNumber(day["low"].as<const char*>());
  forecast.weatherCode = mapWeatherText(day["type"].as<const char*>());
  return forecast;
}

}  // namespace

WeatherError WeatherCnProvider::fetch(const LocationConfig& location, WeatherSnapshot& out,
                                      WeatherDiagnostics* diagnostics) {
  (void)location;
  if (cityId_.empty()) return WeatherError::MISSING_FIELD;

  std::string url = "http://t.weather.itboy.net/api/weather/city/";
  url += cityId_;

  const HttpResponse response = transport_.get(url);
  copyDiagnostics(response, diagnostics);
  const WeatherError transportError = mapTransportError(response.error);
  if (transportError != WeatherError::NONE) return transportError;
  if (response.statusCode != 200) return WeatherError::HTTP_STATUS;

  DynamicJsonDocument document(8192);
  if (deserializeJson(document, response.body)) return WeatherError::PARSE;

  const JsonObjectConst data = document["data"].as<JsonObjectConst>();
  if (data.isNull()) return WeatherError::MISSING_FIELD;
  const char* wendu = data["wendu"].as<const char*>();
  const char* shidu = data["shidu"].as<const char*>();
  const JsonArrayConst forecast = data["forecast"].as<JsonArrayConst>();
  if (wendu == nullptr || shidu == nullptr || forecast.isNull() || forecast.size() < 2) {
    return WeatherError::MISSING_FIELD;
  }

  const JsonObjectConst today = forecast[0].as<JsonObjectConst>();
  if (today.isNull()) return WeatherError::MISSING_FIELD;

  WeatherSnapshot parsed;
  parsed.currentTemp = parseLeadingNumber(wendu);
  parsed.apparentTemp = parsed.currentTemp;
  parsed.humidityPercent = static_cast<int>(parseLeadingNumber(shidu));
  parsed.windSpeed = windSpeedFromLevel(today["fl"].as<const char*>());
  parsed.precipitationProbabilityPercent = kProbabilityUnavailable;
  parsed.weatherCode = mapWeatherText(today["type"].as<const char*>());

  if (!finiteRange(parsed.currentTemp, -100.0f, 70.0f) || parsed.humidityPercent < 0 ||
      parsed.humidityPercent > 100 || !finiteRange(parsed.windSpeed, 0.0f, 500.0f) ||
      parsed.weatherCode < 0 || parsed.weatherCode > 99) {
    return WeatherError::PARSE;
  }

  parsed.today = parseForecastDay(today);
  parsed.tomorrow = parseForecastDay(forecast[1].as<JsonObjectConst>());
  if (forecast.size() >= 3) {
    parsed.dayAfter = parseForecastDay(forecast[2].as<JsonObjectConst>());
  } else {
    parsed.dayAfter = parsed.tomorrow;
  }

  out = parsed;
  return WeatherError::NONE;
}
