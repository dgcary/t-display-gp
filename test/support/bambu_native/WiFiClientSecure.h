#pragma once
#include "Client.h"
#include <utility>
#include <vector>
// Only replace the TLS/socket boundary. Tests exercise the production MQTT
// library and transport adapter with timed incoming bytes, never real secrets.
class WiFiClientSecure : public Client {
 public:
  bool socketOpen = false;
  bool connectOk = true;
  std::vector<std::pair<uint32_t, uint8_t>> incoming;
  std::vector<uint8_t> written;
  size_t cursor = 0;
  int connect(const char*, uint16_t) override { socketOpen = connectOk; return connectOk; }
  int connect(IPAddress, uint16_t) override { socketOpen = connectOk; return connectOk; }
  int available() override {
    if (!socketOpen) return 0;
    size_t end = cursor;
    const uint32_t now = millis();
    while (end < incoming.size() && incoming[end].first <= now) ++end;
    return static_cast<int>(end-cursor);
  }
  int read() override { return cursor < incoming.size() ? incoming[cursor++].second : -1; }
  int peek() override { return cursor < incoming.size() ? incoming[cursor].second : -1; }
  size_t write(uint8_t data) override { return write(&data, 1); }
  size_t write(const uint8_t* data, size_t length) override {
    if (!socketOpen) return 0;
    written.insert(written.end(), data, data+length);
    return length;
  }
  uint8_t connected() override {
    // Arduino's connected() probes read(..., 0), which dispatches available().
    available();
    return socketOpen;
  }
  void stop() override { socketOpen = false; }
  void flush() override {}
};
