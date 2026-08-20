#include "HomeAssistantProvider.h"

#include <ArduinoJson.h>

namespace {
void fillDiagnostics(const HttpResponse& response, HomeAssistantDiagnostics* diagnostics) {
  if (!diagnostics) return;
  diagnostics->httpStatus = response.statusCode;
  diagnostics->nativeError = response.nativeError;
  diagnostics->tlsError = response.tlsError;
  diagnostics->expectedBytes = response.expectedBytes;
  diagnostics->receivedBytes = response.receivedBytes;
  diagnostics->elapsedMs = response.elapsedMs;
}

HomeAssistantError mapTransportError(const HttpResponse& response) {
  if (response.statusCode == 401) return HomeAssistantError::UNAUTHORIZED;
  switch (response.error) {
    case HttpTransportError::NONE: return HomeAssistantError::NONE;
    case HttpTransportError::HTTP_STATUS: return HomeAssistantError::HTTP_STATUS;
    case HttpTransportError::BODY_TOO_LARGE: return HomeAssistantError::BODY_TOO_LARGE;
    case HttpTransportError::NETWORK:
    case HttpTransportError::TRUNCATED_BODY: return HomeAssistantError::NETWORK;
  }
  return HomeAssistantError::NETWORK;
}
}  // namespace

HomeAssistantError HomeAssistantProvider::fetch(const HomeAssistantConfig& config,
                                                const HomeAssistantEntityConfig& entity,
                                                HomeAssistantEntitySnapshot& out,
                                                HomeAssistantDiagnostics* diagnostics) {
  const HttpResponse response = transport_.get(config, entity);
  fillDiagnostics(response, diagnostics);
  const HomeAssistantError transportError = mapTransportError(response);
  if (transportError != HomeAssistantError::NONE) return transportError;

  DynamicJsonDocument doc(4096);
  const DeserializationError parseError = deserializeJson(doc, response.body);
  if (parseError) return HomeAssistantError::PARSE;

  const char* entityId = doc["entity_id"] | nullptr;
  const char* state = doc["state"] | nullptr;
  if (!entityId || !state) return HomeAssistantError::MISSING_FIELD;
  if (entity.entityId != entityId) return HomeAssistantError::ENTITY_MISMATCH;

  HomeAssistantEntitySnapshot parsed;
  parsed.entityId = entityId;
  parsed.state = state;
  if (parsed.state.size() > 64) return HomeAssistantError::PARSE;
  const char* friendly = doc["attributes"]["friendly_name"] | "";
  const char* unit = doc["attributes"]["unit_of_measurement"] | "";
  parsed.friendlyName = friendly;
  parsed.unit = unit;
  if (parsed.friendlyName.size() > 80 || parsed.unit.size() > 24) return HomeAssistantError::PARSE;
  out = std::move(parsed);
  return HomeAssistantError::NONE;
}
