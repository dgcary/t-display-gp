#include "BambuSessionModel.h"

#include <cstddef>

namespace {
constexpr uint32_t REL0 = 60000U;
constexpr uint32_t REL1 = 300000U;
constexpr uint32_t REL2 = 900000U;
constexpr uint32_t REL3 = 1800000U;
constexpr uint32_t BACKOFFS[] = {REL0, REL1, REL2, REL3};

uint32_t backoffForFailureCount(uint8_t failureCount) {
  if (failureCount == 0U) return 0U;
  const size_t index = static_cast<size_t>(failureCount - 1U);
  const size_t capped = index < (sizeof(BACKOFFS) / sizeof(BACKOFFS[0]))
                            ? index
                            : (sizeof(BACKOFFS) / sizeof(BACKOFFS[0]) - 1U);
  return BACKOFFS[capped];
}

bool retryableState(BambuSessionState state) {
  return state == BambuSessionState::RELOGIN_PENDING ||
         state == BambuSessionState::LOGIN_FAILED ||
         state == BambuSessionState::NETWORK_ERROR;
}
}  // namespace

void BambuSessionModel::setAutomaticReloginAvailable(bool available) {
  automaticReloginAvailable_ = available;
  if (!available) {
    reloginScheduled_ = false;
    if (retryableState(state_)) state_ = BambuSessionState::TOKEN_INVALID;
  }
}

void BambuSessionModel::onMqttAuthFailure(uint32_t nowMs) {
  state_ = BambuSessionState::TOKEN_INVALID;
  reloginFailureCount_ = 0U;
  reloginAnchorMs_ = nowMs;
  reloginDelayMs_ = 0U;
  reloginScheduled_ = false;

  if (automaticReloginAvailable_) {
    state_ = BambuSessionState::RELOGIN_PENDING;
    reloginScheduled_ = true;
  }
}

bool BambuSessionModel::shouldRelogin(uint32_t nowMs) const {
  if (!automaticReloginAvailable_ || !reloginScheduled_ || !retryableState(state_)) {
    return false;
  }
  return static_cast<uint32_t>(nowMs - reloginAnchorMs_) >= reloginDelayMs_;
}

void BambuSessionModel::onReloginStarted() {
  if (!automaticReloginAvailable_ || !reloginScheduled_ || !retryableState(state_)) return;
  reloginScheduled_ = false;
  state_ = BambuSessionState::RELOGIN_IN_PROGRESS;
}

void BambuSessionModel::onReloginSuccess() {
  state_ = BambuSessionState::MQTT_CONNECTING;
  reloginScheduled_ = false;
  reloginFailureCount_ = 0U;
  reloginAnchorMs_ = 0U;
  reloginDelayMs_ = 0U;
}

void BambuSessionModel::onReloginFailure(uint32_t nowMs, BambuReloginFailure reason) {
  if (reason == BambuReloginFailure::TWO_FACTOR_REQUIRED) {
    state_ = BambuSessionState::TWO_FACTOR_REQUIRED;
    reloginScheduled_ = false;
    reloginDelayMs_ = 0U;
    return;
  }

  if (reloginFailureCount_ < 0xFFU) ++reloginFailureCount_;
  reloginAnchorMs_ = nowMs;
  reloginDelayMs_ = backoffForFailureCount(reloginFailureCount_);
  reloginScheduled_ = automaticReloginAvailable_;

  state_ = reason == BambuReloginFailure::INVALID_CREDENTIALS
               ? BambuSessionState::LOGIN_FAILED
               : BambuSessionState::NETWORK_ERROR;
}
