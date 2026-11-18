#pragma once

#include <memory>

#include "BambuCloudClient.h"
#include "BambuMqttService.h"
#include "HomeAssistantConfig.h"

class AppConfig;
class ConfigStore;

class IntegrationConfigPortal {
 public:
  IntegrationConfigPortal();
  ~IntegrationConfigPortal();
  IntegrationConfigPortal(const IntegrationConfigPortal&) = delete;
  IntegrationConfigPortal& operator=(const IntegrationConfigPortal&) = delete;

  void begin(HomeAssistantConfig& homeAssistantConfig,
             BambuCloudClient& bambuCloud,
             BambuMqttService& bambuService,
             AppConfig& appConfig,
             ConfigStore& configStore);
  void process();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
