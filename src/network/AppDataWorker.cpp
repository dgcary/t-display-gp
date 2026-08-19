#include "AppDataWorker.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <memory>
#include <new>

#include "HttpTransport.h"
#include "WeatherProvider.h"

namespace {
const char* errorName(WeatherError error) {
  switch (error) {
    case WeatherError::NONE: return "OK";
    case WeatherError::NETWORK: return "NETWORK";
    case WeatherError::HTTP_STATUS: return "HTTP";
    case WeatherError::BODY_TOO_LARGE: return "BODY_TOO_LARGE";
    case WeatherError::PARSE: return "PARSE";
    case WeatherError::MISSING_FIELD: return "MISSING_FIELD";
  }
  return "UNKNOWN";
}
}

struct AppDataWorker::Impl {
  static constexpr UBaseType_t REQUEST_QUEUE_DEPTH = 4;
  static constexpr UBaseType_t RESULT_QUEUE_DEPTH = 4;
  static constexpr uint32_t WORKER_STACK_BYTES = 8192;
  static constexpr UBaseType_t WORKER_PRIORITY = 1;
  static constexpr BaseType_t WORKER_CORE = 0;

  HttpTransport transport;
  OpenMeteoProvider weather{transport};
  QueueHandle_t requestQueue = nullptr;
  QueueHandle_t resultQueue = nullptr;
  TaskHandle_t workerTask = nullptr;
  bool started = false;

  static void taskEntry(void* arg) {
    static_cast<Impl*>(arg)->run();
  }

  void sendResult(const AppDataResult& result) {
    if (!resultQueue) return;
    xQueueSend(resultQueue, &result, portMAX_DELAY);
  }

  void execute(std::unique_ptr<AppDataRequest> request) {
    if (!request) return;
    const uint32_t startedMs = millis();
    const uint32_t queueWaitMs = static_cast<uint32_t>(startedMs - request->createdMs);
    AppDataResult result;
    result.requestId = request->requestId;
    result.type = request->type;

    if (request->type == AppDataRequestType::WEATHER) {
      result.error = weather.fetch(request->location, result.weather, &result.diagnostics);
      Serial.printf("[appdata] id=%lu type=WEATHER location=%s queue=%lums dur=%lums http=%d native=%d tls=%d bytes=%u/%ld result=%s\n",
                    static_cast<unsigned long>(request->requestId), request->location.displayName.c_str(),
                    static_cast<unsigned long>(queueWaitMs),
                    static_cast<unsigned long>(result.diagnostics.elapsedMs),
                    result.diagnostics.httpStatus, result.diagnostics.nativeError,
                    result.diagnostics.tlsError,
                    static_cast<unsigned>(result.diagnostics.receivedBytes),
                    static_cast<long>(result.diagnostics.expectedBytes), errorName(result.error));
    }
    sendResult(result);
  }

  void run() {
    while (true) {
      AppDataRequest* incoming = nullptr;
      if (xQueueReceive(requestQueue, &incoming, portMAX_DELAY) == pdTRUE && incoming) {
        execute(std::unique_ptr<AppDataRequest>(incoming));
      }
    }
  }

  void cleanup() {
    AppDataRequest* request = nullptr;
    if (requestQueue) {
      while (xQueueReceive(requestQueue, &request, 0) == pdTRUE) delete request;
      vQueueDelete(requestQueue);
      requestQueue = nullptr;
    }
    if (resultQueue) {
      vQueueDelete(resultQueue);
      resultQueue = nullptr;
    }
  }
};

AppDataWorker::AppDataWorker() : impl_(new Impl()) {}

AppDataWorker::~AppDataWorker() {
  if (!impl_) return;
  if (impl_->workerTask) vTaskDelete(impl_->workerTask);
  impl_->cleanup();
}

bool AppDataWorker::begin() {
  if (impl_->started) return true;
  impl_->requestQueue = xQueueCreate(Impl::REQUEST_QUEUE_DEPTH, sizeof(AppDataRequest*));
  impl_->resultQueue = xQueueCreate(Impl::RESULT_QUEUE_DEPTH, sizeof(AppDataResult));
  if (!impl_->requestQueue || !impl_->resultQueue) {
    impl_->cleanup();
    return false;
  }

  const BaseType_t created = xTaskCreatePinnedToCore(
      Impl::taskEntry, "app-data", Impl::WORKER_STACK_BYTES, impl_.get(), Impl::WORKER_PRIORITY,
      &impl_->workerTask, Impl::WORKER_CORE);
  if (created != pdPASS) {
    impl_->workerTask = nullptr;
    impl_->cleanup();
    return false;
  }
  impl_->started = true;
  return true;
}

bool AppDataWorker::enqueue(const AppDataRequest& request) {
  if (!impl_->started || !impl_->requestQueue) return false;
  AppDataRequest* copy = new (std::nothrow) AppDataRequest(request);
  if (!copy) return false;
  if (xQueueSend(impl_->requestQueue, &copy, 0) != pdTRUE) {
    delete copy;
    return false;
  }
  return true;
}

bool AppDataWorker::tryReceive(AppDataResult& result) {
  if (!impl_->started || !impl_->resultQueue) return false;
  return xQueueReceive(impl_->resultQueue, &result, 0) == pdTRUE;
}
