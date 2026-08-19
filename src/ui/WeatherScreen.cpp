#include "WeatherScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>
#include <ctime>

#include "UiTheme.h"
#include "WeatherVisuals.h"

namespace {
constexpr int PET_X0 = 202;
constexpr int PET_Y0 = 26;
constexpr int PET_W = 118;
constexpr int PET_H = 94;

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

void drawDailyCard(TFT_eSPI& tft, U8g2_for_TFT_eSPI& font, int x, const char* label,
                   const DailyForecast& day) {
  tft.fillRoundRect(x, 124, 100, 34, 5, UiTheme::CARD);
  tft.drawRoundRect(x, 124, 100, 34, 5, UiTheme::GRID);
  font.setFont(u8g2_font_wqy12_t_gb2312);
  font.setForegroundColor(conditionColor(day.weatherCode));
  font.setCursor(x + 7, 138);
  font.print(label);

  char values[32] = {};
  std::snprintf(values, sizeof(values), "%.0f / %.0fC", day.highTemp, day.lowTemp);
  tft.setTextColor(UiTheme::TEXT, UiTheme::CARD);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(values, x + 30, 128, 1);
  font.setBackgroundColor(UiTheme::CARD);
  font.setForegroundColor(UiTheme::MUTED);
  font.setCursor(x + 30, 155);
  font.print(conditionName(day.weatherCode));
  font.setBackgroundColor(UiTheme::BACKGROUND);
}

void drawWeatherBackdrop(TFT_eSPI& tft, WeatherVisualKind kind, uint8_t frame) {
  const int drift = frame ? 2 : 0;
  switch (kind) {
    case WeatherVisualKind::CLEAR:
      tft.fillCircle(292, 43 + drift, 9, UiTheme::WEATHER_SUN);
      tft.drawLine(292, 29 + drift, 292, 34 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(292, 52 + drift, 292, 58 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(278, 43 + drift, 283, 43 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(301, 43 + drift, 307, 43 + drift, UiTheme::WEATHER_SUN);
      break;
    case WeatherVisualKind::CLOUDY:
      tft.fillCircle(284 + drift, 42, 8, UiTheme::WEATHER_FOG);
      tft.fillCircle(295 + drift, 39, 11, UiTheme::WEATHER_FOG);
      tft.fillCircle(306 + drift, 43, 7, UiTheme::WEATHER_FOG);
      tft.fillRoundRect(278 + drift, 42, 34, 9, 4, UiTheme::WEATHER_FOG);
      break;
    case WeatherVisualKind::FOG:
      for (int i = 0; i < 3; ++i) {
        tft.drawFastHLine(274 + (i == 1 ? drift : 0), 35 + i * 8, 37, UiTheme::WEATHER_FOG);
      }
      break;
    case WeatherVisualKind::RAIN:
    case WeatherVisualKind::STORM:
      tft.fillCircle(284, 38, 8, UiTheme::WEATHER_FOG);
      tft.fillCircle(296, 36, 10, UiTheme::WEATHER_FOG);
      tft.fillRoundRect(278, 39, 31, 8, 4, UiTheme::WEATHER_FOG);
      tft.drawLine(282, 51 + drift, 278, 58 + drift, UiTheme::WEATHER_RAIN);
      tft.drawLine(294, 51 + drift, 290, 58 + drift, UiTheme::WEATHER_RAIN);
      tft.drawLine(306, 51 + drift, 302, 58 + drift, UiTheme::WEATHER_RAIN);
      if (kind == WeatherVisualKind::STORM) {
        tft.drawLine(297, 48, 291, 59, UiTheme::WEATHER_SUN);
        tft.drawLine(291, 59, 297, 57, UiTheme::WEATHER_SUN);
        tft.drawLine(297, 57, 292, 67, UiTheme::WEATHER_SUN);
      }
      break;
    case WeatherVisualKind::SNOW:
      for (int i = 0; i < 4; ++i) {
        const int x = 280 + i * 9;
        const int y = 36 + ((i + frame) & 1) * 10;
        tft.drawLine(x - 3, y, x + 3, y, UiTheme::WEATHER_COOL);
        tft.drawLine(x, y - 3, x, y + 3, UiTheme::WEATHER_COOL);
      }
      break;
    case WeatherVisualKind::OTHER:
      tft.drawCircle(294, 43, 11, UiTheme::MUTED);
      break;
  }
}

void drawCat(TFT_eSPI& tft, CatMood mood, uint8_t frame) {
  const int bob = frame ? 1 : 0;
  const int headX = 257;
  const int headY = 75 + bob;

  tft.drawLine(278, 94 + bob, frame ? 293 : 289, frame ? 86 : 101, UiTheme::CAT);
  tft.drawLine(279, 95 + bob, frame ? 294 : 290, frame ? 87 : 102, UiTheme::CAT);
  tft.fillRoundRect(241, 83 + bob, 39, 24, 10, UiTheme::CAT);
  tft.fillCircle(headX, headY, 16, UiTheme::CAT);
  tft.fillTriangle(244, 65 + bob, 250, 52 + bob, 255, 66 + bob, UiTheme::CAT);
  tft.fillTriangle(259, 65 + bob, 267, 52 + bob, 271, 68 + bob, UiTheme::CAT);

  tft.fillCircle(252, 80 + bob, 5, UiTheme::CAT_LIGHT);
  tft.fillCircle(262, 80 + bob, 5, UiTheme::CAT_LIGHT);
  tft.fillTriangle(255, 79 + bob, 259, 79 + bob, 257, 82 + bob, UiTheme::CAT_PINK);

  if (mood == CatMood::STARTLED) {
    tft.fillCircle(251, 72 + bob, 3, UiTheme::TEXT);
    tft.fillCircle(263, 72 + bob, 3, UiTheme::TEXT);
    tft.fillCircle(251, 72 + bob, 1, UiTheme::BACKGROUND);
    tft.fillCircle(263, 72 + bob, 1, UiTheme::BACKGROUND);
    tft.drawCircle(257, 86 + bob, 2, UiTheme::BACKGROUND);
  } else if (mood == CatMood::SLEEPY) {
    tft.drawLine(248, 73 + bob, 253, 73 + bob, UiTheme::BACKGROUND);
    tft.drawLine(261, 73 + bob, 266, 73 + bob, UiTheme::BACKGROUND);
    tft.drawLine(254, 86 + bob, 260, 86 + bob, UiTheme::BACKGROUND);
  } else {
    tft.fillCircle(251, 72 + bob, 2, UiTheme::BACKGROUND);
    tft.fillCircle(263, 72 + bob, 2, UiTheme::BACKGROUND);
    if (mood == CatMood::HAPPY) {
      tft.drawLine(257, 83 + bob, 254, 87 + bob, UiTheme::BACKGROUND);
      tft.drawLine(257, 83 + bob, 260, 87 + bob, UiTheme::BACKGROUND);
    } else if (mood == CatMood::HOT) {
      tft.fillCircle(257, 87 + bob, 3, UiTheme::CAT_PINK);
      tft.drawLine(282, 70 + bob, 285, 75 + bob, UiTheme::WEATHER_COOL);
    } else if (mood == CatMood::RAINY) {
      tft.drawLine(254, 87 + bob, 257, 84 + bob, UiTheme::BACKGROUND);
      tft.drawLine(257, 84 + bob, 260, 87 + bob, UiTheme::BACKGROUND);
    } else {
      tft.drawFastHLine(254, 86 + bob, 6, UiTheme::BACKGROUND);
    }
  }

  if (mood == CatMood::COLD) {
    tft.fillRoundRect(241, 87 + bob, 39, 5, 2, UiTheme::WEATHER_COOL);
    tft.drawLine(244, 93 + bob, 239, 103 + bob, UiTheme::WEATHER_COOL);
  }
}
}  // namespace

void WeatherScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void WeatherScreen::drawPetScene(const WeatherViewModel& model, uint8_t animationFrame) {
  if (!display_ || !model.hasData) return;
  display_->fillRect(PET_X0, PET_Y0, PET_W, PET_H, UiTheme::BACKGROUND);
  const WeatherSnapshot& weather = model.weather;
  drawWeatherBackdrop(*display_, WeatherVisuals::kindForCode(weather.weatherCode), animationFrame);
  drawCat(*display_, WeatherVisuals::catMood(weather.weatherCode, weather.apparentTemp), animationFrame);
}

void WeatherScreen::renderAnimation(const WeatherViewModel& model, uint8_t animationFrame) {
  if (!display_ || !unicodeFont_ || !model.configured || !model.hasData) return;
  drawPetScene(model, animationFrame);
}

void WeatherScreen::render(const WeatherViewModel& model, bool, uint8_t animationFrame) {
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
  display_->drawFastHLine(0, 24, 320, UiTheme::GRID);

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
  unicodeFont_->setCursor(10, 43);
  unicodeFont_->print(conditionName(w.weatherCode));

  display_->setTextColor(temperatureColor(w.currentTemp), UiTheme::BACKGROUND);
  display_->setTextDatum(TL_DATUM);
  display_->setTextFont(4);
  display_->setTextSize(1);
  char temp[20] = {};
  std::snprintf(temp, sizeof(temp), "%.1fC", w.currentTemp);
  display_->drawString(temp, 8, 46);

  char line[64] = {};
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  std::snprintf(line, sizeof(line), "体感 %.1fC", w.apparentTemp);
  unicodeFont_->setCursor(10, 91);
  unicodeFont_->print(line);
  unicodeFont_->setForegroundColor(UiTheme::WEATHER_COOL);
  std::snprintf(line, sizeof(line), "湿度 %d%%", w.humidityPercent);
  unicodeFont_->setCursor(100, 91);
  unicodeFont_->print(line);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "风 %.1f km/h", w.windSpeed);
  unicodeFont_->setCursor(10, 108);
  unicodeFont_->print(line);
  unicodeFont_->setForegroundColor(w.precipitationProbabilityPercent >= 50
                                       ? UiTheme::WEATHER_RAIN
                                       : UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "雨 %d%%", w.precipitationProbabilityPercent);
  unicodeFont_->setCursor(105, 108);
  unicodeFont_->print(line);

  char updateTime[16] = {};
  formatUpdateTime(w.updatedEpochSeconds, updateTime, sizeof(updateTime));
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "更新 %s", updateTime);
  unicodeFont_->setCursor(10, 121);
  unicodeFont_->print(line);

  if (model.stale || model.error != WeatherError::NONE) {
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->setCursor(105, 121);
    unicodeFont_->print(model.stale ? "天气延迟" : errorName(model.error));
  }

  drawPetScene(model, animationFrame);
  drawDailyCard(*display_, *unicodeFont_, 4, "今", w.today);
  drawDailyCard(*display_, *unicodeFont_, 110, "明", w.tomorrow);
  drawDailyCard(*display_, *unicodeFont_, 216, "后", w.dayAfter);

  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(8, 169);
  unicodeFont_->print("长按左键返回主菜单");
}
