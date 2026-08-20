#pragma once

#include "HomeAssistantConfig.h"

class HomeAssistantConfigStore {
 public:
  bool load(HomeAssistantConfig& out) const;
  bool save(const HomeAssistantConfig& config) const;
  bool clear() const;
};
