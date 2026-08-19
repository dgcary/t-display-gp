#include "WeatherScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>
#include <ctime>

#include "UiTheme.h"
#include "WeatherCatArt.h"
#include "WeatherVisuals.h"

namespace {
constexpr int PET_X0 = 202;
constexpr int PET_Y0 = 26;
constexpr int PET_W = 118;
constexpr int PET_H = 94;

// Warm, low-saturation RGB565 palette used to imitate layered watercolor
// washes on the small black panel without allocating a bitmap or sprite.
constexpr uint16_t CAT_INK = 0x528A;
constexpr uint16_t CAT_WASH_EDGE = 0xDEB3;
constexpr uint16_t CAT_WASH = 0xF735;
constexpr uint16_t CAT_WASH_LIGHT = 0xFF9A;
constexpr uint16_t CAT_PATCH = 0xC4CC;
constexpr uint16_t CAT_PATCH_LIGHT = 0xE651;
constexpr uint16_t CAT_SHADOW = 0x3186;
constexpr uint16_t CAT_BLUSH = 0xFCD3;
constexpr uint16_t CAT_NOSE = 0xF36E;
constexpr uint16_t CAT_SCARF = 0x5D9F;
constexpr uint16_t CAT_UMBRELLA = 0xE45D;

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
      tft.fillCircle(298, 43 + drift, 8, UiTheme::WEATHER_SUN);
      tft.drawLine(298, 30 + drift, 298, 34 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(298, 52 + drift, 298, 57 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(286, 43 + drift, 290, 43 + drift, UiTheme::WEATHER_SUN);
      tft.drawLine(306, 43 + drift, 311, 43 + drift, UiTheme::WEATHER_SUN);
      break;
    case WeatherVisualKind::CLOUDY:
      tft.fillCircle(288 + drift, 39, 7, UiTheme::WEATHER_FOG);
      tft.fillCircle(298 + drift, 37, 9, UiTheme::WEATHER_FOG);
      tft.fillCircle(307 + drift, 41, 6, UiTheme::WEATHER_FOG);
      tft.fillRoundRect(282 + drift, 40, 31, 8, 4, UiTheme::WEATHER_FOG);
      break;
    case WeatherVisualKind::FOG:
      for (int i = 0; i < 3; ++i) {
        tft.drawFastHLine(279 + (i == 1 ? drift : 0), 34 + i * 8, 34, UiTheme::WEATHER_FOG);
      }
      break;
    case WeatherVisualKind::RAIN:
    case WeatherVisualKind::STORM:
      tft.fillCircle(289, 37, 7, UiTheme::WEATHER_FOG);
      tft.fillCircle(300, 35, 9, UiTheme::WEATHER_FOG);
      tft.fillRoundRect(283, 39, 29, 7, 4, UiTheme::WEATHER_FOG);
      tft.drawLine(287, 50 + drift, 284, 56 + drift, UiTheme::WEATHER_RAIN);
      tft.drawLine(299, 50 + drift, 296, 56 + drift, UiTheme::WEATHER_RAIN);
      tft.drawLine(309, 50 + drift, 306, 56 + drift, UiTheme::WEATHER_RAIN);
      break;
    case WeatherVisualKind::SNOW:
      for (int i = 0; i < 4; ++i) {
        const int x = 283 + i * 8;
        const int y = 35 + ((i + frame) & 1) * 9;
        tft.drawLine(x - 3, y, x + 3, y, UiTheme::WEATHER_COOL);
        tft.drawLine(x, y - 3, x, y + 3, UiTheme::WEATHER_COOL);
      }
      break;
    case WeatherVisualKind::OTHER:
      tft.drawCircle(298, 42, 10, UiTheme::MUTED);
      break;
  }
}

void drawThickLine(TFT_eSPI& tft, int x0, int y0, int x1, int y1, uint16_t color) {
  tft.drawLine(x0, y0, x1, y1, color);
  tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
  tft.drawLine(x0, y0 + 1, x1, y1 + 1, color);
}

void drawUmbrella(TFT_eSPI& tft, int bob) {
  // Three overlapping lobes give the canopy a softer hand-painted edge.
  tft.fillCircle(246, 53 + bob, 10, CAT_UMBRELLA);
  tft.fillCircle(258, 49 + bob, 13, CAT_UMBRELLA);
  tft.fillCircle(271, 53 + bob, 10, CAT_UMBRELLA);
  tft.fillRect(236, 53 + bob, 45, 7, CAT_UMBRELLA);
  tft.drawFastHLine(238, 60 + bob, 41, CAT_INK);
  drawThickLine(tft, 259, 59 + bob, 259, 89 + bob, CAT_INK);
  tft.drawLine(259, 89 + bob, 264, 93 + bob, CAT_INK);
}

void drawWatercolorCat(TFT_eSPI& tft, CatMood mood, uint8_t frame) {
  const WeatherCatPose pose = WeatherCatArt::pose(mood, frame);
  const int bob = pose.bodyBob;
  const int cx = 257;
  const int headY = 73 + bob;

  // Ground shadow and tail. Several neighboring strokes emulate a soft brush.
  tft.fillRoundRect(232, 108, 54, 5, 3, CAT_SHADOW);
  drawThickLine(tft, 278, 95 + bob, 292 + pose.tailOffset, 88 + bob, CAT_WASH_EDGE);
  drawThickLine(tft, 292 + pose.tailOffset, 88 + bob, 296 + pose.tailOffset, 78 + bob, CAT_PATCH);
  tft.fillCircle(296 + pose.tailOffset, 78 + bob, 3, CAT_PATCH_LIGHT);

  // Body wash: dark edge, warm wash and irregular light belly layered together.
  tft.fillRoundRect(238, 84 + bob, 42, 27, 12, CAT_WASH_EDGE);
  tft.fillRoundRect(240, 82 + bob, 39, 27, 12, CAT_WASH);
  tft.fillCircle(258, 96 + bob, 14, CAT_WASH_LIGHT);
  tft.fillCircle(247, 91 + bob, 8, CAT_PATCH_LIGHT);
  tft.fillCircle(246, 92 + bob, 5, CAT_PATCH);
  tft.fillCircle(269, 99 + bob, 7, CAT_PATCH_LIGHT);

  // Small rounded paws keep the silhouette chibi rather than stick-like.
  tft.fillCircle(247, 108 + bob, 6, CAT_WASH_EDGE);
  tft.fillCircle(247, 106 + bob, 5, CAT_WASH_LIGHT);
  tft.fillCircle(270, 108 + bob, 6, CAT_WASH_EDGE);
  tft.fillCircle(270, 106 + bob, 5, CAT_WASH_LIGHT);

  // Ears are drawn before the head so the circular wash softens their roots.
  tft.fillTriangle(239, 66 + bob, 246, 49 + bob, 253, 66 + bob, CAT_WASH_EDGE);
  tft.fillTriangle(261, 65 + bob, 270, 49 + bob, 276, 68 + bob, CAT_WASH_EDGE);
  tft.fillTriangle(243, 64 + bob, 247, 54 + bob, 251, 65 + bob, CAT_NOSE);
  tft.fillTriangle(265, 64 + bob, 270, 54 + bob, 273, 66 + bob, CAT_NOSE);

  // Face: a slightly darker outer wash and two offset cream layers avoid the
  // hard geometric look of the old single orange circle.
  tft.fillCircle(cx, headY, 22, CAT_WASH_EDGE);
  tft.fillCircle(cx - 1, headY - 1, 20, CAT_WASH);
  tft.fillCircle(cx - 3, headY - 3, 16, CAT_WASH_LIGHT);

  // Asymmetric tabby patches give the face a hand-painted character.
  tft.fillCircle(244, 63 + bob, 7, CAT_PATCH_LIGHT);
  tft.fillCircle(246, 64 + bob, 5, CAT_PATCH);
  tft.fillCircle(268, 67 + bob, 6, CAT_PATCH_LIGHT);
  tft.fillCircle(269, 68 + bob, 4, CAT_PATCH);
  tft.drawLine(255, 55 + bob, 253, 61 + bob, CAT_PATCH);
  tft.drawLine(259, 54 + bob, 259, 60 + bob, CAT_PATCH);
  tft.drawLine(263, 55 + bob, 265, 61 + bob, CAT_PATCH);

  // Cream muzzle and nose.
  tft.fillCircle(251, 81 + bob, 7, CAT_WASH_LIGHT);
  tft.fillCircle(263, 81 + bob, 7, CAT_WASH_LIGHT);
  tft.fillTriangle(254, 79 + bob, 260, 79 + bob, 257, 83 + bob, CAT_NOSE);

  // Eyes and expression.
  if (pose.eyesClosed) {
    tft.drawLine(246, 72 + bob, 251, 74 + bob, CAT_INK);
    tft.drawLine(251, 74 + bob, 254, 72 + bob, CAT_INK);
    tft.drawLine(261, 72 + bob, 264, 74 + bob, CAT_INK);
    tft.drawLine(264, 74 + bob, 269, 72 + bob, CAT_INK);
  } else if (pose.eyesWide) {
    tft.fillCircle(250, 72 + bob, 4, CAT_INK);
    tft.fillCircle(265, 72 + bob, 4, CAT_INK);
    tft.fillCircle(249, 70 + bob, 1, UiTheme::TEXT);
    tft.fillCircle(264, 70 + bob, 1, UiTheme::TEXT);
  } else {
    tft.fillCircle(250, 72 + bob, 3, CAT_INK);
    tft.fillCircle(265, 72 + bob, 3, CAT_INK);
    tft.fillCircle(249, 71 + bob, 1, UiTheme::TEXT);
    tft.fillCircle(264, 71 + bob, 1, UiTheme::TEXT);
  }

  if (pose.blush) {
    tft.fillCircle(242, 82 + bob, 3, CAT_BLUSH);
    tft.fillCircle(272, 82 + bob, 3, CAT_BLUSH);
  }

  if (pose.smile) {
    tft.drawLine(257, 84 + bob, 253, 88 + bob, CAT_INK);
    tft.drawLine(257, 84 + bob, 261, 88 + bob, CAT_INK);
  } else if (pose.eyesWide) {
    tft.drawCircle(257, 88 + bob, 2, CAT_INK);
  } else if (pose.accessory == CatAccessory::SWEAT) {
    tft.fillCircle(257, 88 + bob, 3, CAT_NOSE);
    tft.drawFastHLine(254, 85 + bob, 6, CAT_INK);
  } else {
    tft.drawLine(254, 87 + bob, 257, 85 + bob, CAT_INK);
    tft.drawLine(257, 85 + bob, 260, 87 + bob, CAT_INK);
  }

  // Whiskers are deliberately thin so the face stays soft on the 170px panel.
  tft.drawLine(244, 85 + bob, 234, 83 + bob, CAT_INK);
  tft.drawLine(244, 88 + bob, 234, 90 + bob, CAT_INK);
  tft.drawLine(270, 85 + bob, 280, 83 + bob, CAT_INK);
  tft.drawLine(270, 88 + bob, 280, 90 + bob, CAT_INK);

  switch (pose.accessory) {
    case CatAccessory::NONE:
      break;
    case CatAccessory::SWEAT:
      tft.fillCircle(281, 67 + bob, 3, UiTheme::WEATHER_COOL);
      tft.fillTriangle(278, 66 + bob, 284, 66 + bob, 281, 59 + bob, UiTheme::WEATHER_COOL);
      break;
    case CatAccessory::SLEEP_MARK:
      tft.drawLine(279, 58, 288, 58, UiTheme::WEATHER_FOG);
      tft.drawLine(288, 58, 279, 66, UiTheme::WEATHER_FOG);
      tft.drawLine(279, 66, 288, 66, UiTheme::WEATHER_FOG);
      tft.drawLine(290, 48, 297, 48, UiTheme::WEATHER_FOG);
      tft.drawLine(297, 48, 290, 54, UiTheme::WEATHER_FOG);
      tft.drawLine(290, 54, 297, 54, UiTheme::WEATHER_FOG);
      break;
    case CatAccessory::UMBRELLA:
      drawUmbrella(tft, bob);
      break;
    case CatAccessory::SCARF:
      tft.fillRoundRect(239, 88 + bob, 38, 7, 3, CAT_SCARF);
      tft.fillRoundRect(269, 92 + bob, 7, 17, 3, CAT_SCARF);
      tft.drawFastHLine(270, 100 + bob, 5, UiTheme::WEATHER_COOL);
      break;
    case CatAccessory::LIGHTNING:
      tft.drawLine(282, 58, 276, 69, UiTheme::WEATHER_SUN);
      tft.drawLine(276, 69, 283, 67, UiTheme::WEATHER_SUN);
      tft.drawLine(283, 67, 277, 80, UiTheme::WEATHER_SUN);
      tft.drawLine(283, 58, 277, 69, UiTheme::WEATHER_SUN);
      break;
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
  drawWatercolorCat(*display_, WeatherVisuals::catMood(weather.weatherCode, weather.apparentTemp), animationFrame);
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
