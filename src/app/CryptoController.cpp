#include "CryptoController.h"

namespace {
constexpr uint32_t CRYPTO_REFRESH_MS = 60000U;
constexpr uint32_t CRYPTO_STALE_MS = 120000U;
}

void CryptoController::begin() {
  cache_ = {};
  viewModel_ = {};
  lastError_ = CryptoError::NONE;
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

void CryptoController::setActive(bool active) {
  if (active_ == active) return;
  active_ = active;
  if (active_) { dirty_ = true; fullRedraw_ = true; }
}

void CryptoController::setWifiOnline(bool online) {
  if (wifiOnline_ == online) return;
  wifiOnline_ = online;
  dirty_ = true;
}

bool CryptoController::refreshDue(uint32_t nowMs) const {
  return !attempted_ || elapsed(nowMs, lastAttemptMs_) >= CRYPTO_REFRESH_MS;
}

void CryptoController::consumeResults(uint32_t) {
  AppDataResult result;
  while (queue_.tryReceive(AppDataRequestType::CRYPTO, result)) {
    if (result.requestId != outstandingRequestId_) continue;
    outstandingRequestId_ = 0;
    if (result.cryptoError == CryptoError::NONE) {
      cache_ = result.crypto;
      hasData_ = true;
      hasSuccessTime_ = true;
      lastSuccessMs_ = result.completedMs;
      lastError_ = CryptoError::NONE;
    } else {
      lastError_ = result.cryptoError;
    }
    dirty_ = true;
  }
}

void CryptoController::tick(uint32_t nowMs) {
  consumeResults(nowMs);
  if (active_ && wifiOnline_ && outstandingRequestId_ == 0 && refreshDue(nowMs)) {
    AppDataRequest request;
    request.requestId = nextRequestId_++;
    request.type = AppDataRequestType::CRYPTO;
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

void CryptoController::publish(uint32_t nowMs) {
  viewModel_.wifiOnline = wifiOnline_;
  viewModel_.hasData = hasData_;
  viewModel_.requestInFlight = outstandingRequestId_ != 0;
  viewModel_.error = lastError_;
  if (hasData_) viewModel_.snapshot = cache_;
  viewModel_.stale = hasData_ && hasSuccessTime_ && elapsed(nowMs, lastSuccessMs_) >= CRYPTO_STALE_MS;
}

bool CryptoController::takeDirtyFlag() { const bool value = dirty_; dirty_ = false; return value; }
bool CryptoController::takeFullRedrawFlag() { const bool value = fullRedraw_; fullRedraw_ = false; return value; }
