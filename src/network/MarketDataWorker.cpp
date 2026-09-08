#include "MarketDataWorker.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>
#include <new>
#include <utility>
#include <vector>

#include "EastMoneyProvider.h"
#include "HttpTransport.h"
#include "TencentProvider.h"

namespace {
const char* requestTypeName(MarketRequestType type) {
  switch (type) {
    case MarketRequestType::QUOTE: return "QUOTE";
    case MarketRequestType::INTRADAY: return "INTRADAY";
    case MarketRequestType::PRIMARY_PROBE: return "PROBE";
  }
  return "UNKNOWN";
}

const char* providerName(ProviderId provider) {
  return provider == ProviderId::TENCENT ? "TX" : "EM";
}

unsigned maxAttempts(const MarketRequest& request) {
  return request.type == MarketRequestType::INTRADAY && request.provider == ProviderId::TENCENT
             ? BuildConfig::INTRADAY_MAX_ATTEMPTS
             : 1U;
}

const char* errorName(ProviderError error) {
  switch (error) {
    case ProviderError::NONE: return "OK";
    case ProviderError::NETWORK: return "NETWORK";
    case ProviderError::HTTP_STATUS: return "HTTP";
    case ProviderError::BODY_TOO_LARGE: return "BODY_TOO_LARGE";
    case ProviderError::PARSE: return "PARSE";
    case ProviderError::MISSING_FIELD: return "MISSING_FIELD";
    case ProviderError::STALE: return "STALE";
    case ProviderError::UNSUPPORTED: return "UNSUPPORTED";
    case ProviderError::CANCELLED: return "CANCELLED";
    case ProviderError::EXPIRED: return "EXPIRED";
  }
  return "UNKNOWN";
}
}  // namespace

struct MarketDataWorker::Impl {
  static constexpr size_t HIGH_PRIORITY_CAPACITY = 8;
  static constexpr UBaseType_t RESULT_QUEUE_DEPTH = 16;
  static constexpr uint32_t WORKER_STACK_BYTES = 12288;
  static constexpr UBaseType_t WORKER_PRIORITY = 1;
  static constexpr BaseType_t WORKER_CORE = 0;

  HttpTransport transport;
  EastMoneyProvider eastMoney{transport};
  TencentProvider tencent{transport};
  PendingMarketWork pending{HIGH_PRIORITY_CAPACITY};
  std::vector<MarketRequest> cancelled;
  QueueHandle_t resultQueue = nullptr;
  SemaphoreHandle_t pendingMutex = nullptr;
  TaskHandle_t workerTask = nullptr;
  bool started = false;
  bool paused = false;

  void cleanupPrimitives() {
    if (resultQueue) { vQueueDelete(resultQueue); resultQueue = nullptr; }
    if (pendingMutex) { vSemaphoreDelete(pendingMutex); pendingMutex = nullptr; }
  }

  static void taskEntry(void* arg) {
    static_cast<Impl*>(arg)->run();
  }

  IQuoteProvider& providerFor(ProviderId provider) {
    return provider == ProviderId::TENCENT ? static_cast<IQuoteProvider&>(tencent)
                                           : static_cast<IQuoteProvider&>(eastMoney);
  }

  void sendResult(std::unique_ptr<MarketResult> result) {
    if (!result || !resultQueue) return;
    MarketResult* outgoing = result.release();
    if (xQueueSend(resultQueue, &outgoing, portMAX_DELAY) != pdTRUE) delete outgoing;
  }

  void sendSynthetic(const MarketRequest& request, ProviderError error, uint32_t nowMs) {
    std::unique_ptr<MarketResult> result(new (std::nothrow) MarketResult());
    if (!result) return;
    result->requestId = request.requestId;
    result->type = request.type;
    result->provider = request.provider;
    result->attempt = request.attempt;
    result->queueWaitMs = MarketRequestPolicy::elapsed(nowMs, request.createdMs);
    result->error = error;
    Serial.printf("[md] id=%lu type=%s symbol=%s provider=%s attempt=%u/%u queue=%lums dur=0ms http=0 native=0 tls=0 bytes=0/-1 result=%s\n",
                  static_cast<unsigned long>(request.requestId), requestTypeName(request.type),
                  request.symbol.canonical().c_str(), providerName(request.provider),
                  static_cast<unsigned>(request.attempt), maxAttempts(request),
                  static_cast<unsigned long>(result->queueWaitMs), errorName(error));
    sendResult(std::move(result));
  }

  bool installDeferredIntraday(const MarketRequest& request) {
    if (xSemaphoreTake(pendingMutex, portMAX_DELAY) != pdTRUE) return false;
    const bool installed = !paused && pending.installRetryIfEmpty(request);
    xSemaphoreGive(pendingMutex);
    return installed;
  }

  void execute(MarketRequest request) {
    const uint32_t startedMs = millis();
    std::unique_ptr<MarketResult> result(new (std::nothrow) MarketResult());
    if (!result) {
      sendSynthetic(request, ProviderError::NETWORK, startedMs);
      return;
    }

    result->requestId = request.requestId;
    result->type = request.type;
    result->provider = request.provider;
    result->attempt = request.attempt;
    result->queueWaitMs = MarketRequestPolicy::elapsed(startedMs, request.createdMs);

    IQuoteProvider& provider = providerFor(request.provider);
    if (request.type == MarketRequestType::INTRADAY) {
      result->error = provider.fetchIntraday(request.symbol, result->intraday, &result->diagnostics);
      if (result->intraday.size() > 242) result->intraday.resize(242);
    } else {
      result->error = provider.fetchQuote(request.symbol, result->quote, &result->diagnostics);
    }

    Serial.printf("[md] id=%lu type=%s symbol=%s provider=%s attempt=%u/%u queue=%lums dur=%lums http=%d native=%d tls=%d bytes=%u/%ld result=%s\n",
                  static_cast<unsigned long>(request.requestId), requestTypeName(request.type),
                  request.symbol.canonical().c_str(), providerName(request.provider),
                  static_cast<unsigned>(request.attempt), maxAttempts(request),
                  static_cast<unsigned long>(result->queueWaitMs),
                  static_cast<unsigned long>(result->diagnostics.elapsedMs),
                  result->diagnostics.httpStatus, result->diagnostics.nativeError, result->diagnostics.tlsError,
                  static_cast<unsigned>(result->diagnostics.receivedBytes),
                  static_cast<long>(result->diagnostics.expectedBytes), errorName(result->error));

    if (MarketRequestPolicy::shouldRetryIntraday(request, result->error, result->diagnostics)) {
      MarketRequest retry = request;
      retry.attempt = static_cast<uint8_t>(request.attempt + 1U);
      retry.createdMs = millis();
      retry.notBeforeMs = retry.createdMs + MarketRequestPolicy::retryDelayMs(retry.attempt, retry.requestId);
      retry.priority = MarketRequestPriority::INTRADAY_RETRY;
      if (!MarketRequestPolicy::expired(retry, retry.createdMs) && installDeferredIntraday(retry)) return;
    }

    if (MarketRequestPolicy::shouldFallbackIntraday(request, result->error, result->diagnostics)) {
      MarketRequest fallback = request;
      fallback.provider = ProviderId::EAST_MONEY;
      fallback.attempt = 1;
      fallback.createdMs = millis();
      fallback.notBeforeMs = fallback.createdMs;
      fallback.cycleStartedMs = fallback.createdMs;
      fallback.priority = MarketRequestPriority::INTRADAY;
      if (installDeferredIntraday(fallback)) {
        Serial.printf("[md] id=%lu type=INTRADAY symbol=%s fallback=TX->EM\n",
                      static_cast<unsigned long>(request.requestId), request.symbol.canonical().c_str());
        return;
      }
    }

    sendResult(std::move(result));
  }

  void run() {
    while (true) {
      MarketRequest request;
      std::vector<MarketRequest> expired;
      std::vector<MarketRequest> cancelledNow;
      bool haveRequest = false;
      const uint32_t nowMs = millis();

      if (xSemaphoreTake(pendingMutex, portMAX_DELAY) == pdTRUE) {
        cancelledNow.swap(cancelled);
        if (!paused) haveRequest = pending.popNextReady(nowMs, request, expired);
        xSemaphoreGive(pendingMutex);
      }

      for (const auto& item : cancelledNow) sendSynthetic(item, ProviderError::CANCELLED, nowMs);
      for (const auto& item : expired) sendSynthetic(item, ProviderError::EXPIRED, nowMs);

      if (haveRequest) {
        execute(std::move(request));
        continue;
      }

      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
    }
  }
};

MarketDataWorker::MarketDataWorker() : impl_(new Impl()) {}

MarketDataWorker::~MarketDataWorker() {
  if (!impl_) return;
  if (impl_->workerTask) vTaskDelete(impl_->workerTask);
  MarketResult* result = nullptr;
  if (impl_->resultQueue) {
    while (xQueueReceive(impl_->resultQueue, &result, 0) == pdTRUE) delete result;
    vQueueDelete(impl_->resultQueue);
  }
  if (impl_->pendingMutex) vSemaphoreDelete(impl_->pendingMutex);
}

bool MarketDataWorker::begin() {
  if (impl_->started) return true;
  impl_->resultQueue = xQueueCreate(Impl::RESULT_QUEUE_DEPTH, sizeof(MarketResult*));
  impl_->pendingMutex = xSemaphoreCreateMutex();
  if (!impl_->resultQueue || !impl_->pendingMutex) {
    impl_->cleanupPrimitives();
    return false;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      Impl::taskEntry, "market-data", Impl::WORKER_STACK_BYTES, impl_.get(),
      Impl::WORKER_PRIORITY, &impl_->workerTask, Impl::WORKER_CORE);
  if (created != pdPASS) {
    impl_->workerTask = nullptr;
    impl_->cleanupPrimitives();
    return false;
  }
  impl_->started = true;
  return true;
}

void MarketDataWorker::setPaused(bool paused) {
  if (!impl_->started || !impl_->pendingMutex) return;
  if (xSemaphoreTake(impl_->pendingMutex, portMAX_DELAY) != pdTRUE) return;
  impl_->paused = paused;
  xSemaphoreGive(impl_->pendingMutex);
  if (!paused && impl_->workerTask) xTaskNotifyGive(impl_->workerTask);
}

bool MarketDataWorker::enqueue(const MarketRequest& request) {
  if (!impl_->started || !request.symbol.valid()) return false;
  if (xSemaphoreTake(impl_->pendingMutex, 0) != pdTRUE) return false;
  if (impl_->paused) {
    xSemaphoreGive(impl_->pendingMutex);
    return false;
  }
  const PendingAddResult result = impl_->pending.add(request);
  if (result.accepted && result.replaced) impl_->cancelled.push_back(result.replacedRequest);
  xSemaphoreGive(impl_->pendingMutex);
  if (!result.accepted) return false;
  if (impl_->workerTask) xTaskNotifyGive(impl_->workerTask);
  return true;
}

bool MarketDataWorker::tryReceive(MarketResult& result) {
  if (!impl_->started) return false;
  MarketResult* received = nullptr;
  if (xQueueReceive(impl_->resultQueue, &received, 0) != pdTRUE || !received) return false;
  std::unique_ptr<MarketResult> owner(received);
  result = std::move(*owner);
  return true;
}
