#pragma once

#include <memory>

#include "BambuCloudClient.h"
#include "BambuConfig.h"
#include "BambuConfigStore.h"
#include "BambuMqttService.h"
#include "HomeAssistantConfig.h"

class IntegrationConfigPortal {
 public:
  IntegrationConfigPortal();
  ~IntegrationConfigPortal();
  IntegrationConfigPortal(const IntegrationConfigPortal&) = delete;
  IntegrationConfigPortal& operator=(const IntegrationConfigPortal&) = delete;

  void begin(HomeAssistantConfig& homeAssistantConfig,
             BambuConfig& bambuConfig,
             BambuConfigStore& bambuStore,
             BambuCloudClient& bambuCloud,
             BambuMqttService& bambuService);
  void process();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
