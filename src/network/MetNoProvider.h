#pragma once

#include "AppConfig.h"
#include "HttpTransport.h"
#include "WeatherProvider.h"

// met.no locationforecast fallback provider. Used when the primary Open-Meteo
// endpoint fails at the transport level. The compact endpoint is identified by
// a User-Agent per met.no API terms and returns an hourly timeseries that is
// aggregated into daily high/low forecasts in the device timezone.
class MetNoProvider final : public IWeatherProvider {
 public:
  explicit MetNoProvider(IHttpTransport& transport) : transport_(transport) {}
  WeatherError fetch(const LocationConfig& location, WeatherSnapshot& out,
                     WeatherDiagnostics* diagnostics = nullptr) override;

 private:
  IHttpTransport& transport_;
};
