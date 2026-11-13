#include "MetNoProvider.h"

#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

// The compact endpoint does not expose precipitation probability; the fallback
// marks the probability unavailable instead of inventing data.
constexpr int kProbabilityUnavailable = 0;
constexpr long kDeviceTimezoneOffsetSeconds = 8L * 3600L;  // CST-8

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

// Parse "YYYY-MM-DDTHH:MM:SSZ" into UTC epoch seconds.
bool parseUtcEpoch(const char* time, uint64_t& out) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (std::sscanf(time, "%d-%d-%dT%d:%d:%dZ", &year, &month, &day, &hour, &minute, &second) != 6) {
    return false;
  }
  if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 59) {
    return false;
  }
  long long y = year;
  y -= month <= 2;
  const long long era = (y >= 0 ? y : y - 399) / 400;
  const long long yoe = y - era * 400;
  const long long doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long long days = era * 146097 + doe - 719468;
  out = static_cast<uint64_t>(days * 86400LL + hour * 3600LL + minute * 60LL + second);
  return true;
}

// Map a met.no symbol_code to the WMO code space used by the UI. Heaviest
// conditions are checked first; day/night suffixes are irrelevant here.
int mapSymbolCode(const char* symbol) {
  if (!symbol || !*symbol) return 3;
  const std::string code(symbol);
  if (code.find("thunder") != std::string::npos) return 95;
  if (code.find("sleet") != std::string::npos) return 67;
  if (code.find("snowshowers") != std::string::npos) return 85;
  if (code.find("heavysnow") != std::string::npos) return 75;
  if (code.find("lightsnow") != std::string::npos) return 71;
  if (code.find("snow") != std::string::npos) return 73;
  if (code.find("heavyrainshowers") != std::string::npos) return 82;
  if (code.find("rainshowers") != std::string::npos) return 80;
  if (code.find("heavyrain") != std::string::npos) return 65;
  if (code.find("lightrain") != std::string::npos) return 51;
  if (code.find("rain") != std::string::npos) return 63;
  if (code.find("fog") != std::string::npos) return 45;
  if (code.find("clearsky") != std::string::npos) return 0;
  if (code.find("partlycloudy") != std::string::npos) return 2;
  if (code.find("fairlysunny") != std::string::npos) return 1;
  if (code.find("cloudy") != std::string::npos) return 3;
  return 3;
}

struct DayAggregate {
  float highTemp = -1000.0f;
  float lowTemp = 1000.0f;
  int weatherCode = 3;
  int noonDistance = 24;
};

const char* symbolForEntry(JsonObjectConst entry) {
  const JsonObjectConst next1 = entry["data"]["next_1_hours"].as<JsonObjectConst>();
  if (!next1.isNull()) {
    const char* code = next1["summary"]["symbol_code"].as<const char*>();
    if (code && *code) return code;
  }
  const JsonObjectConst next6 = entry["data"]["next_6_hours"].as<JsonObjectConst>();
  if (!next6.isNull()) {
    const char* code = next6["summary"]["symbol_code"].as<const char*>();
    if (code && *code) return code;
  }
  return nullptr;
}

}  // namespace

WeatherError MetNoProvider::fetch(const LocationConfig& location, WeatherSnapshot& out,
                                  WeatherDiagnostics* diagnostics) {
  char latitude[24] = {};
  char longitude[24] = {};
  std::snprintf(latitude, sizeof(latitude), "%.6f", static_cast<double>(location.latitudeE6) / 1000000.0);
  std::snprintf(longitude, sizeof(longitude), "%.6f", static_cast<double>(location.longitudeE6) / 1000000.0);

  std::string url = "https://api.met.no/weatherapi/locationforecast/2.0/compact?lat=";
  url += latitude;
  url += "&lon=";
  url += longitude;

  HttpHeaders headers;
  HttpHeader userAgent;
  userAgent.name = "User-Agent";
  userAgent.value = "TDisplayGP/1.0 github.com/dgcary/t-display-gp";
  headers.push_back(userAgent);

  const HttpResponse response = transport_.get(url, headers);
  copyDiagnostics(response, diagnostics);
  const WeatherError transportError = mapTransportError(response.error);
  if (transportError != WeatherError::NONE) return transportError;
  if (response.statusCode != 200) return WeatherError::HTTP_STATUS;

  // Filtered deserialization keeps only the fields the fallback consumes so
  // the ~39 KB compact response fits in a small memory pool.
  DynamicJsonDocument filterDocument(768);
  filterDocument["properties"]["timeseries"][0]["time"] = true;
  filterDocument["properties"]["timeseries"][0]["data"]["instant"]["details"]["air_temperature"] = true;
  filterDocument["properties"]["timeseries"][0]["data"]["instant"]["details"]["relative_humidity"] = true;
  filterDocument["properties"]["timeseries"][0]["data"]["instant"]["details"]["wind_speed"] = true;
  filterDocument["properties"]["timeseries"][0]["data"]["next_1_hours"]["summary"]["symbol_code"] = true;
  filterDocument["properties"]["timeseries"][0]["data"]["next_6_hours"]["summary"]["symbol_code"] = true;

  DynamicJsonDocument document(20480);
  const DeserializationError jsonError =
      deserializeJson(document, response.body, DeserializationOption::Filter(filterDocument));
  if (jsonError) return WeatherError::PARSE;

  const JsonArrayConst timeseries = document["properties"]["timeseries"].as<JsonArrayConst>();
  if (timeseries.isNull() || timeseries.size() == 0) return WeatherError::MISSING_FIELD;

  const JsonObjectConst first = timeseries[0].as<JsonObjectConst>();
  const char* firstTime = first["time"].as<const char*>();
  const JsonObjectConst firstDetails = first["data"]["instant"]["details"].as<JsonObjectConst>();
  if (firstTime == nullptr || firstDetails.isNull()) return WeatherError::MISSING_FIELD;

  const JsonVariantConst firstTemp = firstDetails["air_temperature"];
  const JsonVariantConst firstHumidity = firstDetails["relative_humidity"];
  const JsonVariantConst firstWind = firstDetails["wind_speed"];
  if (!firstTemp.is<float>() || !firstHumidity.is<float>() || !firstWind.is<float>()) {
    return WeatherError::PARSE;
  }

  WeatherSnapshot parsed;
  uint64_t updatedEpoch = 0;
  if (!parseUtcEpoch(firstTime, updatedEpoch)) return WeatherError::PARSE;
  parsed.updatedEpochSeconds = updatedEpoch;
  parsed.currentTemp = firstTemp.as<float>();
  parsed.humidityPercent = static_cast<int>(firstHumidity.as<float>());
  parsed.windSpeed = firstWind.as<float>();
  // Compact has no feels-like detail; fall back to the measured temperature.
  parsed.apparentTemp = parsed.currentTemp;
  parsed.precipitationProbabilityPercent = kProbabilityUnavailable;
  parsed.weatherCode = mapSymbolCode(symbolForEntry(first));

  if (!finiteRange(parsed.currentTemp, -100.0f, 70.0f) || parsed.humidityPercent < 0 ||
      parsed.humidityPercent > 100 || !finiteRange(parsed.windSpeed, 0.0f, 500.0f) ||
      parsed.weatherCode < 0 || parsed.weatherCode > 99) {
    return WeatherError::PARSE;
  }

  // Aggregate hourly steps into the first three device-timezone days.
  DayAggregate days[3];
  uint64_t dayKeys[3] = {0, 0, 0};
  size_t dayCount = 0;
  for (JsonObjectConst entry : timeseries) {
    const char* time = entry["time"].as<const char*>();
    const JsonVariantConst temp = entry["data"]["instant"]["details"]["air_temperature"];
    if (time == nullptr || !temp.is<float>()) continue;

    uint64_t epoch = 0;
    if (!parseUtcEpoch(time, epoch)) continue;
    const uint64_t localEpoch = epoch + kDeviceTimezoneOffsetSeconds;
    const uint64_t dayKey = localEpoch / 86400ULL;
    const int localHour = static_cast<int>((localEpoch % 86400ULL) / 3600ULL);
    const float temperature = temp.as<float>();

    size_t group = dayCount;
    for (size_t i = 0; i < dayCount; ++i) {
      if (dayKeys[i] == dayKey) {
        group = i;
        break;
      }
    }
    if (group == dayCount) {
      if (dayCount >= 3) continue;
      dayKeys[group] = dayKey;
      ++dayCount;
    }

    DayAggregate& day = days[group];
    if (temperature > day.highTemp) day.highTemp = temperature;
    if (temperature < day.lowTemp) day.lowTemp = temperature;
    const int distance = localHour >= 12 ? localHour - 12 : 12 - localHour;
    if (distance < day.noonDistance) {
      day.noonDistance = distance;
      day.weatherCode = mapSymbolCode(symbolForEntry(entry));
    }
  }
  if (dayCount < 2) return WeatherError::MISSING_FIELD;

  parsed.today = {days[0].highTemp, days[0].lowTemp, days[0].weatherCode};
  parsed.tomorrow = {days[1].highTemp, days[1].lowTemp, days[1].weatherCode};
  if (dayCount >= 3) {
    parsed.dayAfter = {days[2].highTemp, days[2].lowTemp, days[2].weatherCode};
  } else {
    parsed.dayAfter = parsed.tomorrow;
  }

  out = parsed;
  return WeatherError::NONE;
}
