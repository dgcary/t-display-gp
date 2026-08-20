#include "NetworkArbiter.h"

bool NetworkArbiter::begin() {
  if (mutex_) return true;
  mutex_ = xSemaphoreCreateMutex();
  return mutex_ != nullptr;
}

bool NetworkArbiter::lock() {
  return mutex_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void NetworkArbiter::unlock() {
  if (mutex_) xSemaphoreGive(mutex_);
}

NetworkArbiter& sharedNetworkArbiter() {
  static NetworkArbiter instance;
  return instance;
}
