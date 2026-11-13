#include "StockApp.h"

bool StockApp::begin(const AppConfig& config) {
  if (initialized_) return true;
  if (!worker_.begin()) return false;
  worker_.setPaused(true);
  controller_.begin(config);
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  selectedPage_ = OVERVIEW_PAGE_INDEX;
  overviewRefreshPending_ = true;
  initialized_ = true;
  return true;
}

void StockApp::onEnter() {
  active_ = true;
  worker_.setPaused(false);
  if (selectedPage_ == OVERVIEW_PAGE_INDEX) overviewRefreshPending_ = true;
  forceDirty_ = true;
  forceFullRedraw_ = true;
}

void StockApp::onExit() {
  worker_.setPaused(true);
  active_ = false;
}

void StockApp::onButton(InputEvent event) {
  if (!initialized_ || !active_) return;
  if (event == InputEvent::PREV_SHORT) {
    controller_.onButton(ButtonEvent::PREVIOUS);
  } else if (event == InputEvent::NEXT_SHORT) {
    controller_.onButton(ButtonEvent::NEXT);
  }
}

bool StockApp::selectPage(size_t pageIndex) {
  if (pageIndex >= pageCount()) return false;

  bool selected = true;
  if (pageIndex == OVERVIEW_PAGE_INDEX) {
    overviewRefreshPending_ = true;
  } else {
    selected = controller_.selectIndex(pageIndex - 1U);
  }
  if (!selected) return false;

  selectedPage_ = pageIndex;
  if (active_) {
    forceDirty_ = true;
    forceFullRedraw_ = true;
  }
  return true;
}

void StockApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.consumeMarketResults();
  if (selectedPage_ == OVERVIEW_PAGE_INDEX && overviewRefreshPending_) {
    controller_.requestOverviewRefresh(nowMs);
    overviewRefreshPending_ = false;
  }
  controller_.tick(nowMs, device_.localDateTime());
}

bool StockApp::takeDirtyFlag() {
  const bool modelDirty = selectedPage_ == OVERVIEW_PAGE_INDEX
                              ? controller_.takeOverviewDirtyFlag()
                              : controller_.takeDirtyFlag();
  const bool dirty = forceDirty_ || modelDirty;
  forceDirty_ = false;
  return dirty;
}

bool StockApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}

void StockApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  if (selectedPage_ == OVERVIEW_PAGE_INDEX) {
    screen_.renderOverview(controller_.overviewViewModel(), fullRedraw);
  } else {
    screen_.render(controller_.viewModel(), fullRedraw);
  }
}
