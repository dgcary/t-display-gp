#pragma once

#include <memory>

#include "HomeAssistantConfig.h"

class HomeAssistantConfigPortal {
 public:
  HomeAssistantConfigPortal();
  ~HomeAssistantConfigPortal();
  HomeAssistantConfigPortal(const HomeAssistantConfigPortal&) = delete;
  HomeAssistantConfigPortal& operator=(const HomeAssistantConfigPortal&) = delete;

  void begin(HomeAssistantConfig& config);
  void process();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
