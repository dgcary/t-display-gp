#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

struct HomeAssistantEntityConfig {
  std::string entityId;
  std::string label;
};

struct HomeAssistantConfig {
  bool enabled = false;
  std::string baseUrl;
  std::string token;
  std::string caCert;
  uint32_t refreshSeconds = 30;
  std::array<HomeAssistantEntityConfig, 4> entities{};
  size_t entityCount = 0;
};

enum class HomeAssistantConfigError {
  NONE,
  REFRESH_INTERVAL,
  BASE_URL,
  TOKEN,
  CA_CERT,
  ENTITY_COUNT,
  ENTITY_ID,
  LABEL,
};

struct HomeAssistantConfigValidationResult {
  HomeAssistantConfigError error = HomeAssistantConfigError::NONE;
  size_t entityIndex = 0;
  bool ok() const { return error == HomeAssistantConfigError::NONE; }
  const char* message() const;
};

HomeAssistantConfigValidationResult validateHomeAssistantConfig(const HomeAssistantConfig& config);

namespace HomeAssistantConfigCodec {
bool encode(const HomeAssistantConfig& config, std::string& out);
bool decode(std::string_view json, HomeAssistantConfig& out);
}  // namespace HomeAssistantConfigCodec
