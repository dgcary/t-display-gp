#pragma once

#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// PubSubClient 2.8 polls Client::available() while awaiting CONNACK and
// individual packet bytes. Its connect loop has no yield; readByte's yield()
// does not block an ESP32 priority-1 task long enough for priority-0 IDLE0.
// Sleep one actual scheduler tick on empty reads, including fragmented reports.
// TLS ownership, CA verification, bytes and all existing timeouts are unchanged.
class BambuMqttTransport final : public WiFiClientSecure {
 public:
  int available() override {
    const int count = WiFiClientSecure::available();
    if (count <= 0) vTaskDelay(1);
    return count;
  }
};
