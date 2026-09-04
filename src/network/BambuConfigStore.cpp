#include "BambuConfigStore.h"

#include <Preferences.h>

#include <string>
#include <utility>

#include "build_config.h"

namespace {
constexpr size_t BAMBU_CONFIG_MAX_BYTES = BambuConfigLimits::ENCODED;
}

bool BambuConfigStore::load(BambuConfig& out) const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::BAMBU_CONFIG_NAMESPACE, true)) return false;

  const size_t length = preferences.getBytesLength(BuildConfig::BAMBU_CONFIG_KEY);
  if (length == 0U || length > BAMBU_CONFIG_MAX_BYTES) {
    preferences.end();
    return false;
  }

  std::string encoded(length, '\0');
  const size_t read = preferences.getBytes(
      BuildConfig::BAMBU_CONFIG_KEY, encoded.data(), encoded.size());
  preferences.end();
  if (read != encoded.size()) return false;

  BambuConfig parsed;
  if (!BambuConfigCodec::decode(encoded, parsed)) return false;
  out = std::move(parsed);
  return true;
}

bool BambuConfigStore::save(const BambuConfig& config) const {
  std::string encoded;
  if (!BambuConfigCodec::encode(config, encoded) ||
      encoded.empty() || encoded.size() > BAMBU_CONFIG_MAX_BYTES) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(BuildConfig::BAMBU_CONFIG_NAMESPACE, false)) return false;
  const size_t written = preferences.putBytes(
      BuildConfig::BAMBU_CONFIG_KEY, encoded.data(), encoded.size());
  preferences.end();
  return written == encoded.size();
}

bool BambuConfigStore::clear() const {
  Preferences preferences;
  if (!preferences.begin(BuildConfig::BAMBU_CONFIG_NAMESPACE, false)) return false;

  if (preferences.getBytesLength(BuildConfig::BAMBU_CONFIG_KEY) == 0U) {
    preferences.end();
    return true;
  }

  const bool removed = preferences.remove(BuildConfig::BAMBU_CONFIG_KEY);
  preferences.end();
  return removed;
}
