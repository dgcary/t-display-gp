#include "WeatherApp.h"

#include "BadApplePlayback.h"

bool WeatherApp::begin(const AppConfig& config) {
  if (initialized_) return true;
  controller_.begin(config);
  controller_.setWifiOnline(device_.wifiConnected());
  screen_.begin(device_.display(), device_.unicodeFont());
  initialized_ = true;
  return true;
}

void WeatherApp::onEnter() {
  active_ = true;
  controller_.setActive(true);
  forceDirty_ = true;
  forceFullRedraw_ = true;
  animationInitialized_ = false;
  animationDirty_ = false;
  animationOnlyRender_ = false;
  playbackClockValid_ = false;
  screen_.resetBadApple();
}

void WeatherApp::onExit() {
  active_ = false;
  controller_.setActive(false);
  animationDirty_ = false;
  animationOnlyRender_ = false;
  playbackClockValid_ = false;
}

void WeatherApp::onButton(InputEvent) {
  // Weather has no short-press sub-navigation. Global long-press handling
  // remains owned by AppManager.
}

void WeatherApp::tick(uint32_t nowMs) {
  if (!initialized_ || !active_) return;
  controller_.setWifiOnline(device_.wifiConnected());
  controller_.tick(nowMs);

  if (!controller_.viewModel().hasData) {
    playbackClockValid_ = false;
    return;
  }

  if (!playbackClockValid_) {
    playbackStartMs_ = nowMs;
    playbackClockValid_ = true;
  }
  const uint32_t elapsedMs = static_cast<uint32_t>(nowMs - playbackStartMs_);
  const uint32_t nextFrame = BadApplePlayback::frameIndex(elapsedMs);
  if (!animationInitialized_ || nextFrame != animationFrame_) {
    animationFrame_ = nextFrame;
    animationInitialized_ = true;
    animationDirty_ = true;
  }
}

bool WeatherApp::takeDirtyFlag() {
  const bool controllerDirty = controller_.takeDirtyFlag();
  const bool contentDirty = forceDirty_ || controllerDirty;
  const bool dirty = contentDirty || animationDirty_;

  // A 10 fps Bad Apple frame only redraws the right-side video viewport.
  animationOnlyRender_ = dirty && !contentDirty && animationDirty_;
  if (contentDirty) animationDirty_ = false;
  forceDirty_ = false;
  return dirty;
}

bool WeatherApp::takeFullRedrawFlag() {
  const bool full = forceFullRedraw_ || controller_.takeFullRedrawFlag();
  forceFullRedraw_ = false;
  return full;
}

void WeatherApp::render(bool fullRedraw) {
  if (!initialized_ || !active_) return;
  if (animationOnlyRender_) {
    screen_.renderAnimation(controller_.viewModel(), animationFrame_);
  } else {
    screen_.render(controller_.viewModel(), fullRedraw, animationFrame_);
  }
  animationOnlyRender_ = false;
  animationDirty_ = false;
}
