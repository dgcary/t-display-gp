#pragma once

#include "AppConfig.h"

class ConfigStore {
 public:
  bool load(AppConfig& out) const;
  bool save(const AppConfig& config) const;
  bool clearAppConfig() const;
};
