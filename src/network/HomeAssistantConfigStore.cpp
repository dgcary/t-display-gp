#include "HomeAssistantConfigStore.h"

#include <Preferences.h>

#include <string>

#include "build_config.h"

namespace {
constexpr char HA_CONFIG_KEY[] = "ha_config";
constexpr size_t HA_CONFIG_MAX_BYTES = 6144;
}

bool HomeAssistantConfigStore::load(HomeAssistantConfig& out) const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, true)) return false;
  const size_t length = preferences.getBytesLength(HA_CONFIG_KEY);
  if (length == 0 || length > HA_CONFIG_MAX_BYTES) {
    preferences.end();
    return false;
  }
  std::string encoded(length, '\0');
  const size_t read = preferences.getBytes(HA_CONFIG_KEY, encoded.data(), length);
  preferences.end();
  if (read != length) return false;
  HomeAssistantConfig parsed;
  if (!HomeAssistantConfigCodec::decode(encoded, parsed)) return false;
  out = std::move(parsed);
  return true;
}

bool HomeAssistantConfigStore::save(const HomeAssistantConfig& config) const {
  std::string encoded;
  if (!HomeAssistantConfigCodec::encode(config, encoded) || encoded.size() > HA_CONFIG_MAX_BYTES) return false;
  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, false)) return false;
  const size_t written = preferences.putBytes(HA_CONFIG_KEY, encoded.data(), encoded.size());
  preferences.end();
  return written == encoded.size();
}

bool HomeAssistantConfigStore::clear() const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, false)) return false;
  const bool removed = preferences.remove(HA_CONFIG_KEY);
  preferences.end();
  return removed;
}
