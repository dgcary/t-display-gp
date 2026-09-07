#pragma once

#include <cstdint>
#include <string>

#include "BambuMqttService.h"
#include "BambuState.h"

class TFT_eSPI;
class U8g2_for_TFT_eSPI;

struct BambuViewModel {
  BambuState state;
  BambuMqttStatus service;
  std::string printerName;
};

class BambuScreen {
 public:
  void begin(TFT_eSPI& display, U8g2_for_TFT_eSPI& unicodeFont);
  void render(const BambuViewModel& model, bool fullRedraw);

 private:
  struct RenderSignature {
    std::string printerName;
    BambuSessionState session = BambuSessionState::UNCONFIGURED;
    BambuPrintState printState = BambuPrintState::UNKNOWN;
    uint8_t progress = 0U;
    uint16_t remainingMinutes = 0U;
    float nozzleTemp = 0.0f;
    float nozzleTarget = 0.0f;
    float bedTemp = 0.0f;
    float bedTarget = 0.0f;
    float chamberTemp = 0.0f;
    uint16_t layerNum = 0U;
    uint16_t totalLayers = 0U;
    std::string jobName;
    bool filamentPresent = false;
    bool filamentExternal = false;
    int16_t filamentSlot = -1;
    std::string filamentType;
    int lastMqttRc = -1;
    uint32_t lastUpdateMs = 0U;
  };

  RenderSignature signatureFor(const BambuViewModel& model) const;
  static bool sameHeader(const RenderSignature& a, const RenderSignature& b);
  static bool sameProgress(const RenderSignature& a, const RenderSignature& b);
  static bool sameFilament(const RenderSignature& a, const RenderSignature& b);
  void drawHeader(const BambuViewModel& model);
  void drawProgress(const BambuViewModel& model);
  void drawJob(const BambuViewModel& model);
  void drawFilament(const BambuViewModel& model);
  void drawFooter(const BambuViewModel& model);

  TFT_eSPI* display_ = nullptr;
  U8g2_for_TFT_eSPI* unicodeFont_ = nullptr;
  bool rendered_ = false;
  RenderSignature previous_;
};
