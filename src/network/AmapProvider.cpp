#include "AmapProvider.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// The AMap weather API does not expose precipitation probability; the provider
// marks the probability unavailable instead of inventing data.
constexpr int kProbabilityUnavailable = 0;

// Approximate wind speed (m/s) per Beaufort windpower level reported by AMap.
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

// Map an AMap weather text (UTF-8, e.g. "晴"/"多云"/"雷阵雨") to the WMO code
// space used by the UI. Heaviest conditions are checked first.
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

// AMap reports windpower as a level string ("≤3", "3-4", "5"); approximate a
// wind speed from the leading level digits.
float windSpeedFromPower(const char* power) {
  if (!power || !*power) return 0.0f;
  int level = 0;
  bool sawDigit = false;
  for (const char* p = power; *p; ++p) {
    if (*p >= '0' && *p <= '9') {
      level = level * 10 + (*p - '0');
      sawDigit = true;
    } else if (sawDigit) {
      break;
    }
  }
  if (!sawDigit) return 0.0f;
  if (level < 0) level = 0;
  if (level > 12) level = 12;
  return kWindLevelSpeed[level];
}

WeatherError fetchJson(IHttpTransport& transport, const std::string& url,
                       DynamicJsonDocument& document, WeatherDiagnostics* diagnostics) {
  const HttpResponse response = transport.get(url);
  copyDiagnostics(response, diagnostics);
  const WeatherError transportError = mapTransportError(response.error);
  if (transportError != WeatherError::NONE) return transportError;
  if (response.statusCode != 200) return WeatherError::HTTP_STATUS;
  if (deserializeJson(document, response.body)) return WeatherError::PARSE;
  const char* status = document["status"].as<const char*>();
  if (status == nullptr || std::string(status) != "1") return WeatherError::HTTP_STATUS;
  return WeatherError::NONE;
}

}  // namespace

WeatherError AmapProvider::fetch(const LocationConfig& location, WeatherSnapshot& out,
                                 WeatherDiagnostics* diagnostics) {
  if (key_.empty()) return WeatherError::MISSING_FIELD;

  char latitude[24] = {};
  char longitude[24] = {};
  std::snprintf(latitude, sizeof(latitude), "%.6f", static_cast<double>(location.latitudeE6) / 1000000.0);
  std::snprintf(longitude, sizeof(longitude), "%.6f", static_cast<double>(location.longitudeE6) / 1000000.0);

  // Step 1: resolve the coordinates to an AMap adcode (cached per location).
  if (amapAdcode_.empty() || adcodeLatE6_ != location.latitudeE6 ||
      adcodeLonE6_ != location.longitudeE6) {
    std::string regeoUrl = "https://restapi.amap.com/v3/geocode/regeo?key=";
    regeoUrl += key_;
    regeoUrl += "&location=";
    regeoUrl += longitude;
    regeoUrl.push_back(',');
    regeoUrl += latitude;

    DynamicJsonDocument regeoDocument(4096);
    const WeatherError error = fetchJson(transport_, regeoUrl, regeoDocument, diagnostics);
    if (error != WeatherError::NONE) return error;
    const char* adcode = regeoDocument["regeocode"]["addressComponent"]["adcode"].as<const char*>();
    if (adcode == nullptr || !*adcode) return WeatherError::MISSING_FIELD;
    amapAdcode_ = adcode;
    adcodeLatE6_ = location.latitudeE6;
    adcodeLonE6_ = location.longitudeE6;
  }

  // Step 2: current conditions (lives).
  std::string baseWeatherUrl = "https://restapi.amap.com/v3/weather/weatherInfo?key=";
  baseWeatherUrl += key_;
  baseWeatherUrl += "&city=";
  baseWeatherUrl += amapAdcode_;
  baseWeatherUrl += "&extensions=base";

  WeatherSnapshot parsed;
  {
    DynamicJsonDocument baseDocument(4096);
    const WeatherError error = fetchJson(transport_, baseWeatherUrl, baseDocument, diagnostics);
    if (error != WeatherError::NONE) return error;
    const JsonObjectConst live = baseDocument["lives"][0].as<JsonObjectConst>();
    if (live.isNull()) return WeatherError::MISSING_FIELD;
    const char* liveWeather = live["weather"].as<const char*>();
    const char* liveTemp = live["temperature"].as<const char*>();
    const char* liveHumidity = live["humidity"].as<const char*>();
    const char* liveWindPower = live["windpower"].as<const char*>();
    if (liveWeather == nullptr || liveTemp == nullptr) return WeatherError::MISSING_FIELD;

    parsed.currentTemp = std::strtof(liveTemp, nullptr);
    parsed.apparentTemp = parsed.currentTemp;
    parsed.humidityPercent = liveHumidity ? static_cast<int>(std::strtof(liveHumidity, nullptr)) : 0;
    parsed.windSpeed = windSpeedFromPower(liveWindPower);
    parsed.precipitationProbabilityPercent = kProbabilityUnavailable;
    parsed.weatherCode = mapWeatherText(liveWeather);
  }

  if (!finiteRange(parsed.currentTemp, -100.0f, 70.0f) || parsed.humidityPercent < 0 ||
      parsed.humidityPercent > 100 || !finiteRange(parsed.windSpeed, 0.0f, 500.0f) ||
      parsed.weatherCode < 0 || parsed.weatherCode > 99) {
    return WeatherError::PARSE;
  }

  // Step 3: 3-day forecast (casts: today, tomorrow, day after).
  std::string allWeatherUrl = "https://restapi.amap.com/v3/weather/weatherInfo?key=";
  allWeatherUrl += key_;
  allWeatherUrl += "&city=";
  allWeatherUrl += amapAdcode_;
  allWeatherUrl += "&extensions=all";

  {
    DynamicJsonDocument allDocument(8192);
    const WeatherError error = fetchJson(transport_, allWeatherUrl, allDocument, diagnostics);
    if (error != WeatherError::NONE) return error;
    const JsonArrayConst casts = allDocument["forecasts"][0]["casts"].as<JsonArrayConst>();
    if (casts.isNull() || casts.size() < 2) return WeatherError::MISSING_FIELD;

    const auto parseDay = [](JsonObjectConst day) {
      DailyForecast forecast;
      const char* dayTemp = day["daytemp"].as<const char*>();
      const char* nightTemp = day["nighttemp"].as<const char*>();
      const char* dayWeather = day["dayweather"].as<const char*>();
      if (dayTemp && *dayTemp) forecast.highTemp = std::strtof(dayTemp, nullptr);
      if (nightTemp && *nightTemp) forecast.lowTemp = std::strtof(nightTemp, nullptr);
      forecast.weatherCode = mapWeatherText(dayWeather);
      return forecast;
    };
    parsed.today = parseDay(casts[0].as<JsonObjectConst>());
    parsed.tomorrow = parseDay(casts[1].as<JsonObjectConst>());
    if (casts.size() >= 3) {
      parsed.dayAfter = parseDay(casts[2].as<JsonObjectConst>());
    } else {
      parsed.dayAfter = parsed.tomorrow;
    }
  }

  out = parsed;
  return WeatherError::NONE;
}
