#include "DeviceInfoScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <cstdio>

#include "UiTheme.h"

void DeviceInfoScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void DeviceInfoScreen::render(const DeviceInfoViewModel& model, bool) {
  if (!display_ || !unicodeFont_) return;

  display_->fillScreen(UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(8, 17);
  unicodeFont_->print("设备信息");
  unicodeFont_->setForegroundColor(model.wifiOnline ? UiTheme::MUTED : UiTheme::WARNING);
  unicodeFont_->setCursor(270, 17);
  unicodeFont_->print(model.wifiOnline ? "Wi-Fi" : "离线");
  display_->drawFastHLine(0, 24, 320, UiTheme::GRID);

  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(9, 42);
  unicodeFont_->print("配置 IP");

  display_->setTextDatum(TL_DATUM);
  display_->setTextColor(model.wifiOnline ? UiTheme::ACCENT : UiTheme::WARNING, UiTheme::BACKGROUND);
  display_->setTextFont(2);
  display_->setTextSize(2);
  display_->drawString(model.ip.empty() ? "0.0.0.0" : model.ip.c_str(), 68, 27);
  display_->setTextSize(1);

  char line[96] = {};
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  std::snprintf(line, sizeof(line), "SSID  %s", model.ssid.empty() ? "--" : model.ssid.c_str());
  unicodeFont_->setCursor(9, 71);
  unicodeFont_->print(line);

  std::snprintf(line, sizeof(line), "RSSI  %d dBm", model.rssi);
  unicodeFont_->setForegroundColor(model.rssi >= -67 ? UiTheme::UP : UiTheme::WARNING);
  unicodeFont_->setCursor(9, 91);
  unicodeFont_->print(line);
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(145, 91);
  unicodeFont_->print(("运行  " + DeviceInfoFormatting::uptime(model.uptimeMs)).c_str());

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  std::snprintf(line, sizeof(line), "MAC   %s", model.mac.empty() ? "--" : model.mac.c_str());
  unicodeFont_->setCursor(9, 111);
  unicodeFont_->print(line);

  const std::string heap = DeviceInfoFormatting::kilobytes(model.heapFree);
  const std::string minHeap = DeviceInfoFormatting::kilobytes(model.heapMin);
  const std::string psram = DeviceInfoFormatting::kilobytes(model.psramFree);
  std::snprintf(line, sizeof(line), "Heap %s  Min %s  PS %s", heap.c_str(), minHeap.c_str(), psram.c_str());
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->setCursor(9, 131);
  unicodeFont_->print(line);

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  std::snprintf(line, sizeof(line), "时间  %s", model.localTime.empty() ? "--" : model.localTime.c_str());
  unicodeFont_->setCursor(9, 150);
  unicodeFont_->print(line);

  unicodeFont_->setForegroundColor(UiTheme::ACCENT);
  std::snprintf(line, sizeof(line), "Web: http://%s/", model.ip.empty() ? "0.0.0.0" : model.ip.c_str());
  unicodeFont_->setCursor(9, 167);
  unicodeFont_->print(line);
}
