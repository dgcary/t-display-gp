#include "WeatherProvider.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace {

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

bool numeric(JsonVariantConst value) {
  return value.is<float>() || value.is<double>() || value.is<int>() || value.is<unsigned int>() ||
         value.is<long>() || value.is<unsigned long>() || value.is<long long>() ||
         value.is<unsigned long long>();
}

bool finiteRange(float value, float minValue, float maxValue) {
  return std::isfinite(value) && value >= minValue && value <= maxValue;
}

bool parseDaily(JsonObjectConst daily, size_t index, DailyForecast& out) {
  JsonArrayConst codes = daily["weather_code"].as<JsonArrayConst>();
  JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
  JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
  if (codes.isNull() || highs.isNull() || lows.isNull() || codes.size() <= index ||
      highs.size() <= index || lows.size() <= index) {
    return false;
  }
  const JsonVariantConst code = codes[index];
  const JsonVariantConst high = highs[index];
  const JsonVariantConst low = lows[index];
  if (!numeric(code) || !numeric(high) || !numeric(low)) return false;

  const int weatherCode = code.as<int>();
  const float highTemp = high.as<float>();
  const float lowTemp = low.as<float>();
  if (weatherCode < 0 || weatherCode > 99 || !finiteRange(highTemp, -100.0f, 70.0f) ||
      !finiteRange(lowTemp, -100.0f, 70.0f) || lowTemp > highTemp) {
    return false;
  }
  out = {highTemp, lowTemp, weatherCode};
  return true;
}

}  // namespace

WeatherError OpenMeteoProvider::fetch(const LocationConfig& location, WeatherSnapshot& out,
                                      WeatherDiagnostics* diagnostics) {
  char latitude[24] = {};
  char longitude[24] = {};
  std::snprintf(latitude, sizeof(latitude), "%.6f", static_cast<double>(location.latitudeE6) / 1000000.0);
  std::snprintf(longitude, sizeof(longitude), "%.6f", static_cast<double>(location.longitudeE6) / 1000000.0);

  std::string url = "https://api.open-meteo.com/v1/forecast?latitude=";
  url += latitude;
  url += "&longitude=";
  url += longitude;
  url += "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max";
  url += "&timezone=Asia%2FShanghai&forecast_days=3&timeformat=unixtime";

  const HttpResponse response = transport_.get(url);
  copyDiagnostics(response, diagnostics);
  const WeatherError transportError = mapTransportError(response.error);
  if (transportError != WeatherError::NONE) return transportError;
  if (response.statusCode != 200) return WeatherError::HTTP_STATUS;

  DynamicJsonDocument document(12288);
  const DeserializationError jsonError = deserializeJson(document, response.body);
  if (jsonError) return WeatherError::PARSE;

  const JsonObjectConst current = document["current"].as<JsonObjectConst>();
  const JsonObjectConst daily = document["daily"].as<JsonObjectConst>();
  if (current.isNull() || daily.isNull()) return WeatherError::MISSING_FIELD;

  const char* requiredCurrent[] = {
      "time", "temperature_2m", "relative_humidity_2m", "apparent_temperature",
      "weather_code", "wind_speed_10m"};
  for (const char* key : requiredCurrent) {
    if (!current.containsKey(key)) return WeatherError::MISSING_FIELD;
    if (!numeric(current[key])) return WeatherError::PARSE;
  }

  const JsonArrayConst precipitation = daily["precipitation_probability_max"].as<JsonArrayConst>();
  const JsonArrayConst codes = daily["weather_code"].as<JsonArrayConst>();
  const JsonArrayConst highs = daily["temperature_2m_max"].as<JsonArrayConst>();
  const JsonArrayConst lows = daily["temperature_2m_min"].as<JsonArrayConst>();
  if (precipitation.isNull() || codes.isNull() || highs.isNull() || lows.isNull() ||
      precipitation.size() < 3 || codes.size() < 3 || highs.size() < 3 || lows.size() < 3) {
    return WeatherError::MISSING_FIELD;
  }
  if (!numeric(precipitation[0])) return WeatherError::PARSE;

  WeatherSnapshot parsed;
  parsed.updatedEpochSeconds = current["time"].as<uint64_t>();
  parsed.currentTemp = current["temperature_2m"].as<float>();
  parsed.humidityPercent = current["relative_humidity_2m"].as<int>();
  parsed.apparentTemp = current["apparent_temperature"].as<float>();
  parsed.weatherCode = current["weather_code"].as<int>();
  parsed.windSpeed = current["wind_speed_10m"].as<float>();
  parsed.precipitationProbabilityPercent = precipitation[0].as<int>();

  if (parsed.updatedEpochSeconds == 0 || !finiteRange(parsed.currentTemp, -100.0f, 70.0f) ||
      !finiteRange(parsed.apparentTemp, -120.0f, 90.0f) || parsed.humidityPercent < 0 ||
      parsed.humidityPercent > 100 || parsed.weatherCode < 0 || parsed.weatherCode > 99 ||
      !finiteRange(parsed.windSpeed, 0.0f, 500.0f) || parsed.precipitationProbabilityPercent < 0 ||
      parsed.precipitationProbabilityPercent > 100) {
    return WeatherError::PARSE;
  }

  if (!parseDaily(daily, 0, parsed.today) || !parseDaily(daily, 1, parsed.tomorrow) ||
      !parseDaily(daily, 2, parsed.dayAfter)) {
    return WeatherError::PARSE;
  }

  out = parsed;
  return WeatherError::NONE;
}
