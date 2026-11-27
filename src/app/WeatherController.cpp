#include "WeatherController.h"

#include "build_config.h"

void WeatherController::begin(const AppConfig& config) {
  location_ = config.location;
  config_ = config.weather;
  cache_ = {};
  viewModel_ = {};
  lastError_ = WeatherError::NONE;
  nextRequestId_ = 1;
  outstandingRequestId_ = 0;
  lastAttemptMs_ = 0;
  lastSuccessMs_ = 0;
  active_ = false;
  wifiOnline_ = false;
  attempted_ = false;
  hasData_ = false;
  hasSuccessTime_ = false;
  dirty_ = true;
  fullRedraw_ = true;
  publish(0);
}

void WeatherController::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  if (active_) {
    dirty_ = true;
    fullRedraw_ = true;
  }
}

void WeatherController::setWifiOnline(bool online) {
  if (wifiOnline_ == online) return;
  wifiOnline_ = online;
  dirty_ = true;
  viewModel_.wifiOnline = online;
}

bool WeatherController::configured() const {
  return config_.enabled && !location_.displayName.empty() &&
         location_.latitudeE6 >= -90000000 && location_.latitudeE6 <= 90000000 &&
         location_.longitudeE6 >= -180000000 && location_.longitudeE6 <= 180000000;
}

bool WeatherController::refreshDue(uint32_t nowMs) const {
  if (!attempted_) return true;
  // After a failed attempt retry quickly (2 min) instead of waiting the full
  // refresh interval, so intermittent failures recover without user action.
  const uint32_t interval = lastError_ == WeatherError::NONE
                                ? config_.refreshMinutes * 60U * 1000U
                                : BuildConfig::WEATHER_FAILURE_RETRY_MS;
  return elapsed(nowMs, lastAttemptMs_) >= interval;
}

void WeatherController::consumeResults(uint32_t nowMs) {
  AppDataResult result;
  while (queue_.tryReceive(AppDataRequestType::WEATHER, result)) {
    if (result.requestId != outstandingRequestId_) continue;
    outstandingRequestId_ = 0;
    if (result.error == WeatherError::NONE) {
      cache_ = result.weather;
      hasData_ = true;
      hasSuccessTime_ = true;
      lastSuccessMs_ = result.completedMs;
      lastError_ = WeatherError::NONE;
    } else {
      lastError_ = result.error;
    }
    dirty_ = true;
  }
}

void WeatherController::tick(uint32_t nowMs) {
  consumeResults(nowMs);

  // Retire an in-flight request whose result never arrives (dropped by the
  // worker or a wedged transport) so the normal refresh/retry cadence resumes
  // instead of wedging weather forever.
  if (outstandingRequestId_ != 0 &&
      elapsed(nowMs, lastAttemptMs_) >= BuildConfig::WEATHER_INFLIGHT_TIMEOUT_MS) {
    outstandingRequestId_ = 0;
    dirty_ = true;
  }

  // Fetch and retry regardless of the active page: the controller is ticked
  // from the main loop (WeatherApp::tickBackground), so weather keeps its
  // 2-minute failure retry and refresh cadence while other pages are shown.
  if (configured() && wifiOnline_ && outstandingRequestId_ == 0 && refreshDue(nowMs)) {
    AppDataRequest request;
    request.requestId = nextRequestId_++;
    request.type = AppDataRequestType::WEATHER;
    request.location = location_;
    request.amapKey = config_.amapKey;
    request.weatherCityId = config_.cityId;
    request.createdMs = nowMs;
    if (queue_.enqueue(request)) {
      outstandingRequestId_ = request.requestId;
      attempted_ = true;
      lastAttemptMs_ = nowMs;
      dirty_ = true;
    }
  }

  const bool oldStale = viewModel_.stale;
  publish(nowMs);
  if (oldStale != viewModel_.stale) dirty_ = true;
}

void WeatherController::publish(uint32_t nowMs) {
  viewModel_.configured = configured();
  viewModel_.wifiOnline = wifiOnline_;
  viewModel_.hasData = hasData_;
  viewModel_.requestInFlight = outstandingRequestId_ != 0;
  viewModel_.error = lastError_;
  viewModel_.locationName = location_.displayName;
  if (hasData_) viewModel_.weather = cache_;

  const uint32_t staleMs = config_.refreshMinutes * 2U * 60U * 1000U;
  viewModel_.stale = hasData_ && hasSuccessTime_ && elapsed(nowMs, lastSuccessMs_) >= staleMs;
}

bool WeatherController::takeDirtyFlag() {
  const bool value = dirty_;
  dirty_ = false;
  return value;
}

bool WeatherController::takeFullRedrawFlag() {
  const bool value = fullRedraw_;
  fullRedraw_ = false;
  return value;
}
