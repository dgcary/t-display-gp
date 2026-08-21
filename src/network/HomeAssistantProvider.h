#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "HomeAssistantConfig.h"
#include "HttpTransport.h"

enum class HomeAssistantError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  UNAUTHORIZED,
  BODY_TOO_LARGE,
  PARSE,
  MISSING_FIELD,
  ENTITY_MISMATCH,
};

struct HomeAssistantEntitySnapshot {
  std::string entityId;
  std::string state;
  std::string friendlyName;
  std::string unit;
};

struct HomeAssistantDiagnostics {
  int httpStatus = 0;
  int nativeError = 0;
  int tlsError = 0;
  int32_t expectedBytes = -1;
  size_t receivedBytes = 0;
  uint32_t elapsedMs = 0;
};

class IHomeAssistantTransport {
 public:
  virtual ~IHomeAssistantTransport() = default;
  virtual HttpResponse get(const HomeAssistantConfig& config,
                           const HomeAssistantEntityConfig& entity) = 0;
};

class SecureHomeAssistantTransport final : public IHomeAssistantTransport {
 public:
  HttpResponse get(const HomeAssistantConfig& config,
                   const HomeAssistantEntityConfig& entity) override;
};

class HomeAssistantProvider {
 public:
  explicit HomeAssistantProvider(IHomeAssistantTransport& transport) : transport_(transport) {}
  HomeAssistantError fetch(const HomeAssistantConfig& config, const HomeAssistantEntityConfig& entity,
                           HomeAssistantEntitySnapshot& out,
                           HomeAssistantDiagnostics* diagnostics = nullptr);

 private:
  IHomeAssistantTransport& transport_;
};
