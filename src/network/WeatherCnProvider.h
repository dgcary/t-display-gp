#pragma once

#include <string>

#include "AppConfig.h"
#include "HttpTransport.h"
#include "WeatherProvider.h"

// weather.com.cn city-ID mirror provider (t.weather.itboy.net). Free, no key,
// domestic plain-HTTP source used as the primary weather path when the user
// configures the weather.com.cn city ID; it bypasses the foreign TLS
// interference that affects Open-Meteo and met.no.
class WeatherCnProvider final : public IWeatherProvider {
 public:
  explicit WeatherCnProvider(IHttpTransport& transport) : transport_(transport) {}
  void setCityId(const std::string& cityId) { cityId_ = cityId; }
  bool hasCityId() const { return !cityId_.empty(); }
  WeatherError fetch(const LocationConfig& location, WeatherSnapshot& out,
                     WeatherDiagnostics* diagnostics = nullptr) override;

 private:
  IHttpTransport& transport_;
  std::string cityId_;
};
