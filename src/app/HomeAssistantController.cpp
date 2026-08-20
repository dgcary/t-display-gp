#include "HomeAssistantController.h"

void HomeAssistantController::begin(const HomeAssistantConfig& config) {
  config_ = config;
  viewModel_ = {};
  cache_ = {};
  hasData_ = {};
  errors_ = {};
  nextRequestId_ = 1;
  outstandingRequestId_ = 0;
  lastAttemptMs_ = 0;
  lastSuccessMs_ = 0;
  outstandingEntityIndex_ = 0;
  nextEntityIndex_ = 0;
  active_ = false;
  wifiOnline_ = false;
  attempted_ = false;
  cycleInProgress_ = false;
  hasSuccessTime_ = false;
  dirty_ = true;
  fullRedraw_ = true;
  publish(0);
}

void HomeAssistantController::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  if (active_) { dirty_ = true; fullRedraw_ = true; }
}

void HomeAssistantController::setWifiOnline(bool online) {
  if (wifiOnline_ == online) return;
  wifiOnline_ = online;
  dirty_ = true;
}

bool HomeAssistantController::configured() const {
  return config_.enabled && validateHomeAssistantConfig(config_).ok();
}

bool HomeAssistantController::refreshDue(uint32_t nowMs) const {
  if (!attempted_) return true;
  return elapsed(nowMs, lastAttemptMs_) >= config_.refreshSeconds * 1000U;
}

void HomeAssistantController::consumeResults() {
  AppDataResult result;
  while (queue_.tryReceive(AppDataRequestType::HOME_ASSISTANT, result)) {
    if (result.requestId != outstandingRequestId_ || result.entityIndex != outstandingEntityIndex_) continue;
    outstandingRequestId_ = 0;
    const uint8_t index = result.entityIndex;
    errors_[index] = result.homeAssistantError;
    if (result.homeAssistantError == HomeAssistantError::NONE) {
      cache_[index] = std::move(result.homeAssistant);
      hasData_[index] = true;
      hasSuccessTime_ = true;
      lastSuccessMs_ = result.completedMs;
    }
    nextEntityIndex_ = static_cast<uint8_t>(index + 1U);
    if (nextEntityIndex_ >= config_.entityCount) cycleInProgress_ = false;
    dirty_ = true;
  }
}

bool HomeAssistantController::enqueueEntity(uint8_t index, uint32_t nowMs) {
  if (index >= config_.entityCount) return false;
  AppDataRequest request;
  request.requestId = nextRequestId_++;
  request.type = AppDataRequestType::HOME_ASSISTANT;
  request.homeAssistantConfig = &config_;
  request.entityIndex = index;
  request.createdMs = nowMs;
  if (!queue_.enqueue(request)) return false;
  outstandingRequestId_ = request.requestId;
  outstandingEntityIndex_ = index;
  dirty_ = true;
  return true;
}

void HomeAssistantController::startCycle(uint32_t nowMs) {
  attempted_ = true;
  lastAttemptMs_ = nowMs;
  cycleInProgress_ = true;
  nextEntityIndex_ = 0;
  if (!enqueueEntity(nextEntityIndex_, nowMs)) cycleInProgress_ = false;
}

void HomeAssistantController::tick(uint32_t nowMs) {
  consumeResults();
  if (active_ && configured() && wifiOnline_ && outstandingRequestId_ == 0) {
    if (cycleInProgress_) {
      if (!enqueueEntity(nextEntityIndex_, nowMs)) cycleInProgress_ = false;
    } else if (refreshDue(nowMs)) {
      startCycle(nowMs);
    }
  }
  const bool oldStale = viewModel_.stale;
  publish(nowMs);
  if (oldStale != viewModel_.stale) dirty_ = true;
}

void HomeAssistantController::publish(uint32_t nowMs) {
  viewModel_.configured = configured();
  viewModel_.wifiOnline = wifiOnline_;
  viewModel_.requestInFlight = outstandingRequestId_ != 0 || cycleInProgress_;
  viewModel_.entityCount = config_.entityCount;
  for (size_t i = 0; i < config_.entityCount && i < viewModel_.entities.size(); ++i) {
    auto& view = viewModel_.entities[i];
    view.entityId = config_.entities[i].entityId;
    view.label = config_.entities[i].label.empty() ? config_.entities[i].entityId : config_.entities[i].label;
    view.hasData = hasData_[i];
    view.error = errors_[i];
    if (hasData_[i]) {
      view.state = cache_[i].state;
      view.unit = cache_[i].unit;
      if (config_.entities[i].label.empty() && !cache_[i].friendlyName.empty()) view.label = cache_[i].friendlyName;
    }
  }
  viewModel_.stale = hasSuccessTime_ && elapsed(nowMs, lastSuccessMs_) >= config_.refreshSeconds * 2000U;
}

bool HomeAssistantController::takeDirtyFlag() { const bool value = dirty_; dirty_ = false; return value; }
bool HomeAssistantController::takeFullRedrawFlag() { const bool value = fullRedraw_; fullRedraw_ = false; return value; }
