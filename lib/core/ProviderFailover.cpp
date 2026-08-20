#include "ProviderFailover.h"

void ProviderFailover::recordSuccess(ProviderId provider, uint64_t nowMs) {
  (void)nowMs;
  if (provider != ProviderId::TENCENT) return;

  if (active_ == ProviderId::TENCENT) {
    primaryFailureCount_ = 0;
    firstPrimaryFailureMs_ = 0;
    recoverySuccessCount_ = 0;
    return;
  }

  ++recoverySuccessCount_;
  if (recoverySuccessCount_ >= 2) {
    active_ = ProviderId::TENCENT;
    primaryFailureCount_ = 0;
    firstPrimaryFailureMs_ = 0;
    recoverySuccessCount_ = 0;
    lastPrimaryProbeMs_ = 0;
  }
}

void ProviderFailover::recordFailure(ProviderId provider, uint64_t nowMs) {
  if (provider != ProviderId::TENCENT) return;

  if (active_ == ProviderId::EAST_MONEY) {
    recoverySuccessCount_ = 0;
    return;
  }

  if (primaryFailureCount_ == 0 || nowMs < firstPrimaryFailureMs_ ||
      nowMs - firstPrimaryFailureMs_ > FAILURE_WINDOW_MS) {
    primaryFailureCount_ = 1;
    firstPrimaryFailureMs_ = nowMs;
  } else {
    ++primaryFailureCount_;
  }

  if (primaryFailureCount_ >= 3 && nowMs - firstPrimaryFailureMs_ <= FAILURE_WINDOW_MS) {
    active_ = ProviderId::EAST_MONEY;
    recoverySuccessCount_ = 0;
    lastPrimaryProbeMs_ = nowMs;
  }
}

ProviderId ProviderFailover::activeProvider(uint64_t nowMs) const {
  (void)nowMs;
  return active_;
}

bool ProviderFailover::shouldProbePrimary(uint64_t nowMs) const {
  return active_ == ProviderId::EAST_MONEY && nowMs >= lastPrimaryProbeMs_ &&
         nowMs - lastPrimaryProbeMs_ >= PRIMARY_PROBE_INTERVAL_MS;
}

void ProviderFailover::recordPrimaryProbeAttempt(uint64_t nowMs) {
  if (active_ == ProviderId::EAST_MONEY) {
    lastPrimaryProbeMs_ = nowMs;
  }
}
