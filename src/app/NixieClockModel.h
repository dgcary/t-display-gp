#pragma once

#include <cstdint>

#include "MarketClock.h"

struct NixieClockViewModel {
  bool timeValid = false;
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  int dayOfWeek = -1;
  bool colonVisible = true;
};

namespace NixieClockModel {
inline bool valid(const LocalDateTime& local) {
  return local.year >= 2020 && local.month >= 1 && local.month <= 12 && local.day >= 1 &&
         local.day <= 31 && local.hour >= 0 && local.hour <= 23 && local.minute >= 0 &&
         local.minute <= 59 && local.second >= 0 && local.second <= 59 &&
         local.dayOfWeek >= 0 && local.dayOfWeek <= 6;
}

inline bool colonVisible(uint32_t nowMs) {
  return ((nowMs / 500U) & 1U) == 0U;
}

inline NixieClockViewModel fromLocalDateTime(const LocalDateTime& local, uint32_t nowMs) {
  NixieClockViewModel model;
  model.timeValid = valid(local);
  model.colonVisible = colonVisible(nowMs);
  if (!model.timeValid) return model;

  model.year = local.year;
  model.month = local.month;
  model.day = local.day;
  model.hour = local.hour;
  model.minute = local.minute;
  model.second = local.second;
  model.dayOfWeek = local.dayOfWeek;
  return model;
}

inline const char* weekdayShort(int dayOfWeek) {
  static constexpr const char* DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  return dayOfWeek >= 0 && dayOfWeek <= 6 ? DAYS[dayOfWeek] : "---";
}
}  // namespace NixieClockModel
