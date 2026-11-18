#pragma once

#include <string>

#include "AppConfig.h"
#include "HttpTransport.h"
#include "WeatherProvider.h"

// AMap (gaode) weather provider. Domestic CN-hosted plain-JSON source used as
// the primary weather path when the user configures a Web-service key; it
// bypasses the foreign TLS interference that affects Open-Meteo and met.no.
// Resolves the configured coordinates to an adcode first, then fetches current
// conditions (lives) and the 3-day forecast (casts).
class AmapProvider final : public IWeatherProvider {
 public:
  explicit AmapProvider(IHttpTransport& transport) : transport_(transport) {}
  void setKey(const std::string& key) { key_ = key; }
  bool hasKey() const { return !key_.empty(); }
  WeatherError fetch(const LocationConfig& location, WeatherSnapshot& out,
                     WeatherDiagnostics* diagnostics = nullptr) override;

 private:
  IHttpTransport& transport_;
  std::string key_;
  // Cached adcode for the coordinates it was resolved from; the regeo request
  // is skipped while the configured location is unchanged.
  std::string amapAdcode_;
  int32_t adcodeLatE6_ = 0;
  int32_t adcodeLonE6_ = 0;
};
