#include "BambuScreen.h"

#include <TFT_eSPI.h>
#include <U8g2_for_TFT_eSPI.h>

#include <algorithm>
#include <cstdio>

#include "UiTheme.h"

namespace {
const char* printStateLabel(BambuPrintState state) {
  switch (state) {
    case BambuPrintState::IDLE: return "IDLE";
    case BambuPrintState::RUNNING: return "PRINTING";
    case BambuPrintState::PAUSE: return "PAUSE";
    case BambuPrintState::PREPARE: return "PREPARE";
    case BambuPrintState::FINISH: return "FINISH";
    case BambuPrintState::FAILED: return "FAILED";
    case BambuPrintState::OTHER: return "OTHER";
    case BambuPrintState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

const char* sessionLabel(const BambuViewModel& model) {
  switch (model.service.session) {
    case BambuSessionState::INTEGRATION_DISABLED: return "DISABLED";
    case BambuSessionState::UNCONFIGURED: return "SETUP";
    case BambuSessionState::PRINTER_SELECTION_REQUIRED: return "SELECT PRINTER";
    case BambuSessionState::MQTT_CONNECTING: return "CONNECTING";
    case BambuSessionState::ONLINE: return printStateLabel(model.state.printState);
    case BambuSessionState::TOKEN_INVALID: return "TOKEN EXPIRED";
    case BambuSessionState::RELOGIN_PENDING: return "RELOGIN WAIT";
    case BambuSessionState::RELOGIN_IN_PROGRESS: return "SIGNING IN";
    case BambuSessionState::TWO_FACTOR_REQUIRED: return "2FA REQUIRED";
    case BambuSessionState::LOGIN_FAILED: return "LOGIN FAILED";
    case BambuSessionState::NETWORK_ERROR: return "OFFLINE";
    case BambuSessionState::BUFFER_ERROR: return "BUFFER ERROR";
  }
  return "UNKNOWN";
}

uint16_t statusColor(const BambuViewModel& model) {
  if (model.service.session == BambuSessionState::ONLINE) {
    if (model.state.printState == BambuPrintState::FAILED) return UiTheme::WARNING;
    return UiTheme::UP;
  }
  if (model.service.session == BambuSessionState::MQTT_CONNECTING ||
      model.service.session == BambuSessionState::RELOGIN_PENDING ||
      model.service.session == BambuSessionState::RELOGIN_IN_PROGRESS) {
    return UiTheme::WARNING;
  }
  return UiTheme::MUTED;
}

void formatEta(uint16_t minutes, char* out, size_t size) {
  if (minutes == 0U) {
    std::snprintf(out, size, "ETA --");
    return;
  }
  const unsigned hours = minutes / 60U;
  const unsigned mins = minutes % 60U;
  if (hours > 0U) std::snprintf(out, size, "ETA %uh %02um", hours, mins);
  else std::snprintf(out, size, "ETA %um", mins);
}
}  // namespace

void BambuScreen::begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont) {
  display_ = &display;
  unicodeFont_ = &unicodeFont;
}

void BambuScreen::render(const BambuViewModel& model, bool) {
  if (!display_ || !unicodeFont_) return;

  display_->fillScreen(UiTheme::BACKGROUND);
  unicodeFont_->setFont(u8g2_font_wqy12_t_gb2312);
  unicodeFont_->setBackgroundColor(UiTheme::BACKGROUND);
  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(8, 17);
  unicodeFont_->print(model.printerName.empty() ? "Bambu Lab" : model.printerName.c_str());

  display_->setTextDatum(TR_DATUM);
  display_->setTextFont(2);
  display_->setTextSize(1);
  display_->setTextColor(statusColor(model), UiTheme::BACKGROUND);
  display_->drawString(sessionLabel(model), 312, 5);
  display_->drawFastHLine(0, 24, 320, UiTheme::GRID);

  char text[64] = {};
  std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(model.state.progress));
  display_->setTextDatum(TL_DATUM);
  display_->setTextColor(UiTheme::TEXT, UiTheme::BACKGROUND);
  display_->setTextFont(4);
  display_->setTextSize(2);
  display_->drawString(text, 8, 31);
  display_->setTextSize(1);

  formatEta(model.state.remainingMinutes, text, sizeof(text));
  display_->setTextFont(2);
  display_->setTextColor(UiTheme::MUTED, UiTheme::BACKGROUND);
  display_->drawString(text, 132, 34);

  std::snprintf(text, sizeof(text), "Layer %u/%u",
                static_cast<unsigned>(model.state.layerNum),
                static_cast<unsigned>(model.state.totalLayers));
  display_->drawString(text, 132, 53);

  std::snprintf(text, sizeof(text), "N %.0f/%.0f  B %.0f/%.0f  C %.0f",
                model.state.nozzleTemp, model.state.nozzleTarget,
                model.state.bedTemp, model.state.bedTarget,
                model.state.chamberTemp);
  display_->drawString(text, 132, 72);

  constexpr int barX = 8;
  constexpr int barY = 94;
  constexpr int barW = 304;
  constexpr int barH = 12;
  display_->drawRect(barX, barY, barW, barH, UiTheme::GRID);
  const int inner = barW - 2;
  const int filled = static_cast<int>((static_cast<uint32_t>(std::min<uint8_t>(model.state.progress, 100U)) * inner) / 100U);
  if (filled > 0) display_->fillRect(barX + 1, barY + 1, filled, barH - 2, UiTheme::ACCENT);

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(8, 123);
  unicodeFont_->print("任务  ");
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  unicodeFont_->print(model.state.jobName.empty() ? "--" : model.state.jobName.c_str());

  unicodeFont_->setForegroundColor(UiTheme::TEXT);
  unicodeFont_->setCursor(8, 143);
  unicodeFont_->print("耗材  ");
  unicodeFont_->setForegroundColor(UiTheme::MUTED);
  if (!model.state.filament.present) {
    unicodeFont_->print("--");
  } else {
    char filament[80] = {};
    if (model.state.filament.externalSpool) {
      std::snprintf(filament, sizeof(filament), "External %s",
                    model.state.filament.type.empty() ? "--" : model.state.filament.type.c_str());
    } else {
      std::snprintf(filament, sizeof(filament), "AMS %d  %s",
                    static_cast<int>(model.state.filament.slot + 1),
                    model.state.filament.type.empty() ? "--" : model.state.filament.type.c_str());
    }
    unicodeFont_->print(filament);
  }

  display_->setTextDatum(TL_DATUM);
  display_->setTextFont(1);
  display_->setTextSize(1);
  display_->setTextColor(UiTheme::MUTED, UiTheme::BACKGROUND);
  std::snprintf(text, sizeof(text), "MQTT rc=%d  last=%lus",
                model.service.lastMqttRc,
                static_cast<unsigned long>(model.state.lastUpdateMs / 1000U));
  display_->drawString(text, 8, 157);
}
