#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class NetworkArbiter {
 public:
  bool begin();
  bool lock();
  void unlock();
  bool ready() const { return mutex_ != nullptr; }

 private:
  SemaphoreHandle_t mutex_ = nullptr;
};

NetworkArbiter& sharedNetworkArbiter();
