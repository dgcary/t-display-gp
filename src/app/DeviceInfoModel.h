#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

struct DeviceInfoViewModel {
  std::string ip;
  std::string ssid;
  std::string mac;
  std::string localTime;
  int rssi = 0;
  uint32_t uptimeMs = 0;
  uint32_t heapFree = 0;
  uint32_t heapMin = 0;
  uint32_t psramFree = 0;
  uint32_t psramTotal = 0;
  bool wifiOnline = false;
};

namespace DeviceInfoFormatting {
inline std::string uptime(uint32_t milliseconds) {
  const uint32_t totalSeconds = milliseconds / 1000U;
  const uint32_t days = totalSeconds / 86400U;
  const uint32_t hours = (totalSeconds / 3600U) % 24U;
  const uint32_t minutes = (totalSeconds / 60U) % 60U;
  const uint32_t seconds = totalSeconds % 60U;
  char buffer[24] = {};
  if (days > 0) {
    std::snprintf(buffer, sizeof(buffer), "%lud %02lu:%02lu:%02lu",
                  static_cast<unsigned long>(days), static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
  } else {
    std::snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
                  static_cast<unsigned long>(hours), static_cast<unsigned long>(minutes),
                  static_cast<unsigned long>(seconds));
  }
  return buffer;
}

inline std::string kilobytes(uint32_t bytes) {
  char buffer[20] = {};
  std::snprintf(buffer, sizeof(buffer), "%lu KB",
                static_cast<unsigned long>(bytes / 1024U));
  return buffer;
}
}  // namespace DeviceInfoFormatting
