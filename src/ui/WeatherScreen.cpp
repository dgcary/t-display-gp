#include "WeatherScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>
#include <ctime>

#include "UiTheme.h"
#include "WeatherVisuals.h"
#include "BadAppleAsset.h"

namespace {
constexpr int BAD_APPLE_X = 152;
constexpr int BAD_APPLE_Y = 27;
constexpr int BAD_APPLE_W = 168;
constexpr int BAD_APPLE_H = 126;

const char* conditionName(int code) {
  if (code == 0) return "晴";
  if (code >= 1 && code <= 3) return "多云";
  if (code == 45 || code == 48) return "雾";
  if (code >= 51 && code <= 57) return "毛毛雨";
  if (code >= 61 && code <= 67) return "雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code >= 80 && code <= 82) return "阵雨";
  if (code >= 85 && code <= 86) return "阵雪";
  if (code >= 95) return "雷雨";
  return "天气";
}

const char* errorName(WeatherError error) {
  switch (error) {
    case WeatherError::NONE: return "";
    case WeatherError::NETWORK: return "网络失败";
    case WeatherError::HTTP_STATUS: return "服务异常";
    case WeatherError::BODY_TOO_LARGE: return "响应过大";
    case WeatherError::PARSE: return "数据异常";
    case WeatherError::MISSING_FIELD: return "数据不完整";
  }
  return "天气异常";
}

uint16_t conditionColor(int code) {
  switch (WeatherVisuals::kindForCode(code)) {
    case WeatherVisualKind::CLEAR: return UiTheme::WEATHER_SUN;
    case WeatherVisualKind::RAIN:
    case WeatherVisualKind::STORM: return UiTheme::WEATHER_RAIN;
    case WeatherVisualKind::SNOW: return UiTheme::WEATHER_COOL;
    case WeatherVisualKind::FOG: return UiTheme::WEATHER_FOG;
    case WeatherVisualKind::CLOUDY: return UiTheme::WEATHER_COOL;
    case WeatherVisualKind::OTHER: return UiTheme::MUTED;
  }
  return UiTheme::MUTED;
}

uint16_t temperatureColor(float temp) {
  if (temp >= 32.0f) return UiTheme::WEATHER_WARM;
  if (temp <= 10.0f) return UiTheme::WEATHER_COOL;
  return UiTheme::WEATHER_SUN;
}

void formatUpdateTime(uint64_t epochSeconds, char* out, size_t outSize) {
  if (!out || outSize == 0) return;
  if (epochSeconds == 0) {
    std::snprintf(out, outSize, "--:--");
    return;
  }
  const std::time_t chinaEpoch = static_cast<std::time_t>(epochSeconds + 8ULL * 3600ULL);
  std::tm utc{};
  if (!gmtime_r(&chinaEpoch, &utc)) {
    std::snprintf(out, outSize, "--:--");
    return;
  }
  std::snprintf(out, outSize, "%02d:%02d", utc.tm_hour, utc.tm_min);
}

void drawForecastRow(U8g2_for_TFT_eSPI& font, int y, const char* label, const DailyForecast& day) {
  char line[48] = {};
  std::snprintf(line, sizeof(line), "%s %.0f/%.0fC %s", label, day.highTemp, day.lowTemp,
                conditionName(day.weatherCode));
  font.setFont(u8g2_font_wqy12_t_gb2312);
  font.setForegroundColor(conditionColor(day.weatherCode));
  font.setCursor(8, y);
  font.print(line);
}
}  // namespace

void WeatherScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void WeatherScreen::resetBadApple() {
  badAppleFrameValid_ = false;
  decodedBadAppleFrame_ = 0;
}

bool WeatherScreen::decodeBadApple(uint32_t targetFrame) {
  if (targetFrame >= BadApplePlayback::FRAME_COUNT) return false;

  if (!badAppleFrameValid_ || targetFrame < decodedBadAppleFrame_) {
    for (size_t i = 0; i < badAppleFrame_.size(); ++i) {
      badAppleFrame_[i] = BadAppleAsset::FIRST_FRAME[i];
    }
    decodedBadAppleFrame_ = 0;
    badAppleFrameValid_ = true;
  }

  for (uint32_t frame = decodedBadAppleFrame_ + 1; frame <= targetFrame; ++frame) {
    const uint32_t start = BadAppleAsset::DELTA_OFFSETS[frame - 1];
    const uint32_t end = BadAppleAsset::DELTA_OFFSETS[frame];
    if (end < start || end > BadAppleAsset::DELTA_DATA_SIZE) {
      badAppleFrameValid_ = false;
      return false;
    }
    if (!BadApplePlayback::applyDelta(badAppleFrame_.data(), badAppleFrame_.size(),
                                      BadAppleAsset::DELTA_DATA + start, end - start)) {
      badAppleFrameValid_ = false;
      return false;
    }
    decodedBadAppleFrame_ = frame;
  }
  return true;
}

void WeatherScreen::drawBadApple(uint32_t targetFrame) {
  if (!display_) return;
  if (!decodeBadApple(targetFrame)) {
    display_->fillRect(BAD_APPLE_X, BAD_APPLE_Y, BAD_APPLE_W, BAD_APPLE_H, TFT_BLACK);
    display_->setTextColor(UiTheme::WARNING, TFT_BLACK);
    display_->setTextDatum(MC_DATUM);
    display_->drawString("BAD APPLE DATA", BAD_APPLE_X + BAD_APPLE_W / 2,
                         BAD_APPLE_Y + BAD_APPLE_H / 2, 1);
    return;
  }

  uint16_t row[BAD_APPLE_W];
  for (int y = 0; y < BAD_APPLE_H; ++y) {
    const size_t rowOffset = static_cast<size_t>(y) * BadApplePlayback::ROW_BYTES;
    for (int x = 0; x < BAD_APPLE_W; ++x) {
      const uint8_t packed = badAppleFrame_[rowOffset + static_cast<size_t>(x >> 3)];
      row[x] = (packed & static_cast<uint8_t>(0x80U >> (x & 7))) ? TFT_WHITE : TFT_BLACK;
    }
    display_->pushImage(BAD_APPLE_X, BAD_APPLE_Y + y, BAD_APPLE_W, 1, row);
  }
}

void WeatherScreen::renderAnimation(const WeatherViewModel& model, uint32_t animationFrame) {
  if (!display_ || !unicodeFont_ || !model.configured || !model.hasData) return;
  drawBadApple(animationFrame);
}

void WeatherScreen::render(const WeatherViewModel& model, bool, uint32_t animationFrame) {
  if (!display_ || !unicodeFont_) return;

  display_->fillScreen(UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);

  unicodeFont_->setCursor(8, 17);
  unicodeFont_->print(model.locationName.empty() ? "天气" : model.locationName.c_str());
  unicodeFont_->setForegroundColor(model.wifiOnline ? UiTheme::UP : UiTheme::WARNING);
  unicodeFont_->setCursor(270, 17);
  unicodeFont_->print(model.wifiOnline ? "Wi-Fi" : "离线");

  if (!model.configured) {
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->setCursor(72, 80);
    unicodeFont_->print("天气未配置，请打开 Web 设置");
    unicodeFont_->setForegroundColor(UiTheme::MUTED);
    unicodeFont_->setCursor(65, 108);
    unicodeFont_->print("可在设备信息中查看配置 IP");
    return;
  }

  if (!model.hasData) {
    unicodeFont_->setForegroundColor(UiTheme::MUTED);
    unicodeFont_->setCursor(118, 78);
    unicodeFont_->print(model.requestInFlight ? "正在获取天气..." : "暂无天气数据");
    if (model.error != WeatherError::NONE) {
      unicodeFont_->setForegroundColor(UiTheme::WARNING);
      unicodeFont_->setCursor(124, 104);
      unicodeFont_->print(errorName(model.error));
    }
    return;
  }

  const WeatherSnapshot& w = model.weather;

  unicodeFont_->setForegroundColor(conditionColor(w.weatherCode));
  unicodeFont_->setCursor(8, 42);
  unicodeFont_->print(conditionName(w.weatherCode));

  display_->setTextColor(temperatureColor(w.currentTemp), UiTheme::BACKGROUND);
  display_->setTextDatum(TL_DATUM);
  char temp[20] = {};
  std::snprintf(temp, sizeof(temp), "%.1fC", w.currentTemp);
  display_->drawString(temp, 8, 46, 4);

  char line[48] = {};
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  std::snprintf(line, sizeof(line), "体感 %.1f", w.apparentTemp);
  unicodeFont_->setCursor(8, 88);
  unicodeFont_->print(line);
  unicodeFont_->setForegroundColor(UiTheme::WEATHER_COOL);
  std::snprintf(line, sizeof(line), "湿度 %d%%", w.humidityPercent);
  unicodeFont_->setCursor(78, 88);
  unicodeFont_->print(line);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "风 %.1f", w.windSpeed);
  unicodeFont_->setCursor(8, 104);
  unicodeFont_->print(line);
  unicodeFont_->setForegroundColor(w.precipitationProbabilityPercent >= 50
                                       ? UiTheme::WEATHER_RAIN
                                       : UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "雨 %d%%", w.precipitationProbabilityPercent);
  unicodeFont_->setCursor(78, 104);
  unicodeFont_->print(line);

  char updateTime[16] = {};
  formatUpdateTime(w.updatedEpochSeconds, updateTime, sizeof(updateTime));
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "更新 %s", updateTime);
  unicodeFont_->setCursor(8, 120);
  unicodeFont_->print(line);
  if (model.stale || model.error != WeatherError::NONE) {
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->setCursor(80, 120);
    unicodeFont_->print(model.stale ? "延迟" : errorName(model.error));
  }

  drawForecastRow(*unicodeFont_, 138, "今", w.today);
  drawForecastRow(*unicodeFont_, 152, "明", w.tomorrow);
  drawBadApple(animationFrame);

  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(8, 169);
  unicodeFont_->print("长按左键返回主菜单");
}
