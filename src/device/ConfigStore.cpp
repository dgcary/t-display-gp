#include "ConfigStore.h"

#include <Preferences.h>

#include <string>
#include <utility>

#include "build_config.h"

namespace {
constexpr char APP_CONFIG_KEY[] = "app_config";
}

bool ConfigStore::load(AppConfig& out) const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, true)) return false;
  const String stored = preferences.getString(APP_CONFIG_KEY, "");
  preferences.end();
  if (stored.isEmpty()) return false;

  AppConfig parsed;
  uint32_t sourceSchemaVersion = 0;
  if (!AppConfigCodec::decode(std::string_view(stored.c_str(), stored.length()), parsed,
                              &sourceSchemaVersion) ||
      !validate(parsed).ok()) {
    return false;
  }

  out = parsed;
  if (sourceSchemaVersion == 1) {
    // Best-effort persistence of the normalized v2 shape. A write failure must
    // not discard the already decoded and valid in-memory configuration.
    save(parsed);
  }
  return true;
}

bool ConfigStore::save(const AppConfig& config) const {
  if (!validate(config).ok()) return false;
  std::string encoded;
  if (!AppConfigCodec::encode(config, encoded)) return false;

  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, false)) return false;
  const size_t written = preferences.putString(APP_CONFIG_KEY, encoded.c_str());
  preferences.end();
  return written == encoded.size();
}

bool ConfigStore::clearAppConfig() const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::CONFIG_NAMESPACE, false)) return false;
  const bool removed = preferences.remove(APP_CONFIG_KEY);
  preferences.end();
  return removed;
}
