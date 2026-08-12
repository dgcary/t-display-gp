#pragma once

#include <cstdint>

#include "QuoteModels.h"

class ProviderFailover {
 public:
  void recordSuccess(ProviderId provider, uint64_t nowMs);
  void recordFailure(ProviderId provider, uint64_t nowMs);
  ProviderId activeProvider(uint64_t nowMs) const;

  bool shouldProbePrimary(uint64_t nowMs) const;
  void recordPrimaryProbeAttempt(uint64_t nowMs);

 private:
  static constexpr uint64_t FAILURE_WINDOW_MS = 60000;
  static constexpr uint64_t PRIMARY_PROBE_INTERVAL_MS = 120000;

  ProviderId active_ = ProviderId::EAST_MONEY;
  uint32_t primaryFailureCount_ = 0;
  uint64_t firstPrimaryFailureMs_ = 0;
  uint32_t recoverySuccessCount_ = 0;
  uint64_t lastPrimaryProbeMs_ = 0;
};
