#pragma once

#include <cstdint>

#include "AppConfig.h"
#include "WeatherProvider.h"

enum class AppDataRequestType {
  WEATHER,
};

struct AppDataRequest {
  uint32_t requestId = 0;
  AppDataRequestType type = AppDataRequestType::WEATHER;
  LocationConfig location;
  uint32_t createdMs = 0;
};

struct AppDataResult {
  uint32_t requestId = 0;
  AppDataRequestType type = AppDataRequestType::WEATHER;
  WeatherError error = WeatherError::NONE;
  WeatherSnapshot weather;
  WeatherDiagnostics diagnostics;
};

class IAppDataQueue {
 public:
  virtual ~IAppDataQueue() = default;
  virtual bool enqueue(const AppDataRequest& request) = 0;
  virtual bool tryReceive(AppDataResult& result) = 0;
};
