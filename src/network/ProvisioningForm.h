#pragma once

#include <array>
#include <string>

#include "AppConfig.h"

struct ProvisioningFields {
  std::array<std::string, 5> symbols{};
  std::array<std::string, 5> names{};
  std::string refresh = "5";
};

namespace ProvisioningForm {
bool buildConfig(const ProvisioningFields& fields, AppConfig& out, std::string& error);
ProvisioningFields fromConfig(const AppConfig& config);
}  // namespace ProvisioningForm
