#pragma once

#include <memory>

#include "AppConfig.h"

class ProvisioningService {
 public:
  ProvisioningService();
  ~ProvisioningService();
  ProvisioningService(const ProvisioningService&) = delete;
  ProvisioningService& operator=(const ProvisioningService&) = delete;

  bool ensureConnected(AppConfig& config);
  void beginWebPortal(AppConfig& config);
  void process();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
