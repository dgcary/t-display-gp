#include "BambuApp.h"

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

void BambuApp::onButton(InputEvent) {}

void BambuApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  if (!hasRefresh_ || static_cast<uint32_t>(nowMs - lastRefreshMs_) >= 500U) {
    model_.state = service_.snapshot();
    model_.service = service_.status();
    const BambuConfig config = service_.configSnapshot();
    const BambuPrinterConfig* activePrinter = activeBambuPrinter(config);
    model_.printerName = activePrinter
                             ? (activePrinter->name.empty() ? activePrinter->serial : activePrinter->name)
                             : std::string{};
    lastRefreshMs_ = nowMs;
    hasRefresh_ = true;
    dirty_ = true;
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
