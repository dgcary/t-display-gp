#include "MarketDataWorker.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <memory>
#include <new>
#include <utility>

#include "EastMoneyProvider.h"
#include "HttpTransport.h"
#include "TencentProvider.h"

struct MarketDataWorker::Impl {
  static constexpr UBaseType_t QUEUE_DEPTH = 8;
  static constexpr uint32_t WORKER_STACK_BYTES = 12288;
  static constexpr UBaseType_t WORKER_PRIORITY = 1;
  static constexpr BaseType_t WORKER_CORE = 0;

  HttpTransport transport;
  EastMoneyProvider eastMoney{transport};
  TencentProvider tencent{transport};
  PendingMarketRequests pending{QUEUE_DEPTH};
  QueueHandle_t requestQueue = nullptr;
  QueueHandle_t resultQueue = nullptr;
  SemaphoreHandle_t pendingMutex = nullptr;
  TaskHandle_t workerTask = nullptr;
  bool started = false;

  void cleanupPrimitives() {
    if (requestQueue) { vQueueDelete(requestQueue); requestQueue = nullptr; }
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

  void removePending(const MarketRequest& request) {
    if (!pendingMutex) return;
    if (xSemaphoreTake(pendingMutex, portMAX_DELAY) == pdTRUE) {
      pending.remove(request);
      xSemaphoreGive(pendingMutex);
    }
  }

  void run() {
    while (true) {
      MarketRequest* rawRequest = nullptr;
      if (xQueueReceive(requestQueue, &rawRequest, portMAX_DELAY) != pdTRUE || !rawRequest) continue;
      std::unique_ptr<MarketRequest> request(rawRequest);
      std::unique_ptr<MarketResult> result(new (std::nothrow) MarketResult());
      if (result) {
        result->requestId = request->requestId;
        result->type = request->type;
        result->provider = request->provider;
        IQuoteProvider& provider = providerFor(request->provider);
        if (request->type == MarketRequestType::INTRADAY) {
          result->error = provider.fetchIntraday(request->symbol, result->intraday, &result->diagnostics);
          if (result->intraday.size() > 242) result->intraday.resize(242);
        } else {
          result->error = provider.fetchQuote(request->symbol, result->quote, &result->diagnostics);
        }
      }

      removePending(*request);
      if (!result) continue;
      MarketResult* outgoing = result.release();
      if (xQueueSend(resultQueue, &outgoing, 0) != pdTRUE) delete outgoing;
    }
  }
};

MarketDataWorker::MarketDataWorker() : impl_(new Impl()) {}

MarketDataWorker::~MarketDataWorker() {
  if (!impl_) return;
  if (impl_->workerTask) vTaskDelete(impl_->workerTask);
  MarketRequest* request = nullptr;
  if (impl_->requestQueue) {
    while (xQueueReceive(impl_->requestQueue, &request, 0) == pdTRUE) delete request;
    vQueueDelete(impl_->requestQueue);
  }
  MarketResult* result = nullptr;
  if (impl_->resultQueue) {
    while (xQueueReceive(impl_->resultQueue, &result, 0) == pdTRUE) delete result;
    vQueueDelete(impl_->resultQueue);
  }
  if (impl_->pendingMutex) vSemaphoreDelete(impl_->pendingMutex);
}

bool MarketDataWorker::begin() {
  if (impl_->started) return true;
  impl_->requestQueue = xQueueCreate(Impl::QUEUE_DEPTH, sizeof(MarketRequest*));
  impl_->resultQueue = xQueueCreate(Impl::QUEUE_DEPTH, sizeof(MarketResult*));
  impl_->pendingMutex = xSemaphoreCreateMutex();
  if (!impl_->requestQueue || !impl_->resultQueue || !impl_->pendingMutex) {
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

bool MarketDataWorker::enqueue(const MarketRequest& request) {
  if (!impl_->started || !request.symbol.valid()) return false;
  if (xSemaphoreTake(impl_->pendingMutex, 0) != pdTRUE) return false;
  const bool accepted = impl_->pending.add(request);
  xSemaphoreGive(impl_->pendingMutex);
  if (!accepted) return false;

  MarketRequest* queued = new (std::nothrow) MarketRequest(request);
  if (!queued) {
    impl_->removePending(request);
    return false;
  }
  if (xQueueSend(impl_->requestQueue, &queued, 0) != pdTRUE) {
    impl_->removePending(request);
    delete queued;
    return false;
  }
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
