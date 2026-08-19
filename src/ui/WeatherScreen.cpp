#include "WeatherScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>
#include <ctime>

#include "UiTheme.h"

namespace {
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

void drawDaily(U8g2_for_TFT_eSPI& font, int x, const char* label, const DailyForecast& day) {
  char value[32] = {};
  std::snprintf(value, sizeof(value), "%s %.0f/%.0f", label, day.highTemp, day.lowTemp);
  font.setCursor(x, 143);
  font.print(value);
}
}

void WeatherScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void WeatherScreen::render(const WeatherViewModel& model, bool) {
  if (!display_ || !unicodeFont_) return;

  display_->fillScreen(UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);

  unicodeFont_->setCursor(8, 17);
  unicodeFont_->print(model.locationName.empty() ? "天气" : model.locationName.c_str());
  unicodeFont_->setForegroundColor(model.wifiOnline ? UiTheme::MUTED : UiTheme::WARNING);
  unicodeFont_->setCursor(260, 17);
  unicodeFont_->print(model.wifiOnline ? "Wi-Fi" : "离线");
  display_->drawFastHLine(0, 24, 320, UiTheme::GRID);

  if (!model.configured) {
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->setCursor(72, 80);
    unicodeFont_->print("天气未配置，请打开 Web 设置");
    unicodeFont_->setForegroundColor(UiTheme::MUTED);
    unicodeFont_->setCursor(65, 108);
    unicodeFont_->print("填写地点、经纬度并启用天气");
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
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(12, 51);
  unicodeFont_->print(conditionName(w.weatherCode));

  display_->setTextColor(UiTheme::TEXT, UiTheme::BACKGROUND);
  display_->setTextDatum(TL_DATUM);
  display_->setTextFont(4);
  display_->setTextSize(1);
  char temp[20] = {};
  std::snprintf(temp, sizeof(temp), "%.1fC", w.currentTemp);
  display_->drawString(temp, 75, 32);

  char line[64] = {};
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  std::snprintf(line, sizeof(line), "体感 %.1fC   湿度 %d%%", w.apparentTemp, w.humidityPercent);
  unicodeFont_->setCursor(12, 79);
  unicodeFont_->print(line);
  std::snprintf(line, sizeof(line), "风速 %.1f km/h   降雨 %d%%", w.windSpeed,
                w.precipitationProbabilityPercent);
  unicodeFont_->setCursor(12, 101);
  unicodeFont_->print(line);

  char updateTime[16] = {};
  formatUpdateTime(w.updatedEpochSeconds, updateTime, sizeof(updateTime));
  std::snprintf(line, sizeof(line), "更新 %s", updateTime);
  unicodeFont_->setCursor(12, 121);
  unicodeFont_->print(line);

  if (model.stale || model.error != WeatherError::NONE) {
    unicodeFont_->setForegroundColor(UiTheme::WARNING);
    unicodeFont_->setCursor(222, 121);
    unicodeFont_->print(model.stale ? "天气延迟" : errorName(model.error));
  }

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  drawDaily(*unicodeFont_, 8, "今", w.today);
  drawDaily(*unicodeFont_, 112, "明", w.tomorrow);
  drawDaily(*unicodeFont_, 218, "后", w.dayAfter);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(8, 165);
  unicodeFont_->print("长按左键返回主菜单");
}
