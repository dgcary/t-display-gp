#include "NetworkArbiter.h"

#include "build_config.h"

bool NetworkArbiter::begin() {
  if (mutex_) return true;
  mutex_ = xSemaphoreCreateMutex();
  return mutex_ != nullptr;
}

bool NetworkArbiter::lock() {
  // Bounded wait: a wedged holder (e.g. a socket stuck after WiFi roaming) must
  // not stall the shared appdata worker forever. Callers such as
  // HttpTransport::get treat a failed lock as a NETWORK error and retry.
  return mutex_ &&
         xSemaphoreTake(mutex_, pdMS_TO_TICKS(BuildConfig::NETWORK_ARBITER_LOCK_TIMEOUT_MS)) == pdTRUE;
}

void NetworkArbiter::unlock() {
  if (mutex_) xSemaphoreGive(mutex_);
}

NetworkArbiter& sharedNetworkArbiter() {
  static NetworkArbiter instance;
  return instance;
}
