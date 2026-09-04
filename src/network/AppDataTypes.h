#pragma once

#include <cstdint>

#include "AppConfig.h"
#include "HomeAssistantConfig.h"
#include "HomeAssistantProvider.h"
#include "WeatherProvider.h"

enum class AppDataRequestType {
  WEATHER,
  HOME_ASSISTANT,
};

struct AppDataRequest {
  uint32_t requestId = 0;
  AppDataRequestType type = AppDataRequestType::WEATHER;
  LocationConfig location;
  const HomeAssistantConfig* homeAssistantConfig = nullptr;
  uint8_t entityIndex = 0;
  uint32_t createdMs = 0;
};

struct AppDataResult {
  uint32_t requestId = 0;
  AppDataRequestType type = AppDataRequestType::WEATHER;
  WeatherError error = WeatherError::NONE;
  WeatherSnapshot weather;
  WeatherDiagnostics diagnostics;
  HomeAssistantError homeAssistantError = HomeAssistantError::NONE;
  uint8_t entityIndex = 0;
  HomeAssistantEntitySnapshot homeAssistant;
  HomeAssistantDiagnostics homeAssistantDiagnostics;
  uint32_t completedMs = 0;
};

class IAppDataQueue {
 public:
  virtual ~IAppDataQueue() = default;
  virtual bool enqueue(const AppDataRequest& request) = 0;
  virtual bool tryReceive(AppDataRequestType type, AppDataResult& result) = 0;
};
