#include "BambuApp.h"

#include <utility>

namespace {
bool presentationChanged(const BambuViewModel& before, const BambuViewModel& after) {
  return before.state.lastUpdateMs != after.state.lastUpdateMs ||
         before.state.connected != after.state.connected ||
         before.service.session != after.service.session ||
         before.service.mqttConnected != after.service.mqttConnected ||
         before.service.lastMqttRc != after.service.lastMqttRc ||
         before.printerName != after.printerName;
}
}  // namespace

bool BambuApp::begin() {
  if (initialized_) return true;
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void BambuApp::onEnter() {
  active_ = true;
  hasRefresh_ = false;
  dirty_ = true;
  fullRedraw_ = true;
}

void BambuApp::onExit() {
  active_ = false;
}

void BambuApp::onButton(InputEvent event) {
  int direction = 0;
  if (event == InputEvent::PREV_SHORT) direction = -1;
  else if (event == InputEvent::NEXT_SHORT) direction = 1;
  if (direction == 0) return;

  const bool switched = direction < 0 ? service_.cycleActivePrinter(-1)
                                      : service_.cycleActivePrinter(1);
  if (switched) {
    hasRefresh_ = false;
    dirty_ = true;
    fullRedraw_ = true;
  }
}

size_t BambuApp::pageCount() const {
  const BambuConfig config = service_.configSnapshot();
  const size_t count = config.printerCount;
  return count < MAX_NAVIGATION_PAGES ? count : MAX_NAVIGATION_PAGES;
}

size_t BambuApp::selectedPage() const {
  const BambuConfig config = service_.configSnapshot();
  const size_t count = pageCount();
  return count > 0U && config.activePrinterIndex < count ? config.activePrinterIndex : 0U;
}

bool BambuApp::selectPage(size_t pageIndex) {
  BambuConfig config = service_.configSnapshot();
  const size_t count = config.printerCount < MAX_NAVIGATION_PAGES
                           ? config.printerCount
                           : MAX_NAVIGATION_PAGES;
  if (pageIndex >= count) return false;
  if (config.activePrinterIndex == pageIndex) return true;

  config.activePrinterIndex = pageIndex;
  if (!service_.replaceConfig(config)) return false;
  hasRefresh_ = false;
  dirty_ = true;
  fullRedraw_ = true;
  return true;
}

void BambuApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  if (!hasRefresh_ || static_cast<uint32_t>(nowMs - lastRefreshMs_) >= 500U) {
    BambuViewModel next;
    next.state = service_.snapshot();
    next.service = service_.status();
    const BambuConfig config = service_.configSnapshot();
    const BambuPrinterConfig* activePrinter = activeBambuPrinter(config);
    next.printerName = activePrinter
                           ? (activePrinter->name.empty() ? activePrinter->serial : activePrinter->name)
                           : std::string{};

    const bool changed = !hasRefresh_ || presentationChanged(model_, next);
    model_ = std::move(next);
    lastRefreshMs_ = nowMs;
    hasRefresh_ = true;
    if (changed) dirty_ = true;
  }
}

bool BambuApp::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool BambuApp::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}

void BambuApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  screen_.render(model_, fullRedraw);
}
