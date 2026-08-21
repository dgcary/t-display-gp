#include "AppDataWorker.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <memory>
#include <new>

#include "CryptoProvider.h"
#include "HomeAssistantProvider.h"
#include "HttpTransport.h"
#include "WeatherProvider.h"

namespace {
const char* weatherErrorName(WeatherError error) {
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
const char* cryptoErrorName(CryptoError error) {
  switch (error) {
    case CryptoError::NONE: return "OK";
    case CryptoError::NETWORK: return "NETWORK";
    case CryptoError::HTTP_STATUS: return "HTTP";
    case CryptoError::BODY_TOO_LARGE: return "BODY_TOO_LARGE";
    case CryptoError::PARSE: return "PARSE";
    case CryptoError::MISSING_FIELD: return "MISSING_FIELD";
  }
  return "UNKNOWN";
}
const char* homeAssistantErrorName(HomeAssistantError error) {
  switch (error) {
    case HomeAssistantError::NONE: return "OK";
    case HomeAssistantError::NETWORK: return "NETWORK";
    case HomeAssistantError::HTTP_STATUS: return "HTTP";
    case HomeAssistantError::UNAUTHORIZED: return "UNAUTHORIZED";
    case HomeAssistantError::BODY_TOO_LARGE: return "BODY_TOO_LARGE";
    case HomeAssistantError::PARSE: return "PARSE";
    case HomeAssistantError::MISSING_FIELD: return "MISSING_FIELD";
    case HomeAssistantError::ENTITY_MISMATCH: return "ENTITY_MISMATCH";
  }
  return "UNKNOWN";
}
}

struct AppDataWorker::Impl {
  static constexpr UBaseType_t REQUEST_QUEUE_DEPTH = 6;
  static constexpr UBaseType_t RESULT_QUEUE_DEPTH = 4;
  static constexpr uint32_t WORKER_STACK_BYTES = 10240;
  static constexpr UBaseType_t WORKER_PRIORITY = 1;
  static constexpr BaseType_t WORKER_CORE = 0;

  HttpTransport transport;
  OpenMeteoProvider weather{transport};
  CryptoProvider crypto{transport};
  SecureHomeAssistantTransport homeAssistantTransport;
  HomeAssistantProvider homeAssistant{homeAssistantTransport};
  QueueHandle_t requestQueue = nullptr;
  QueueHandle_t weatherResultQueue = nullptr;
  QueueHandle_t cryptoResultQueue = nullptr;
  QueueHandle_t homeAssistantResultQueue = nullptr;
  TaskHandle_t workerTask = nullptr;
  bool started = false;

  static void taskEntry(void* arg) { static_cast<Impl*>(arg)->run(); }

  QueueHandle_t resultQueueFor(AppDataRequestType type) const {
    switch (type) {
      case AppDataRequestType::WEATHER: return weatherResultQueue;
      case AppDataRequestType::CRYPTO: return cryptoResultQueue;
      case AppDataRequestType::HOME_ASSISTANT: return homeAssistantResultQueue;
    }
    return nullptr;
  }

  void sendResult(std::unique_ptr<AppDataResult> result) {
    if (!result) return;
    QueueHandle_t queue = resultQueueFor(result->type);
    if (!queue) return;
    AppDataResult* raw = result.release();
    if (xQueueSend(queue, &raw, 0) != pdTRUE) {
      Serial.printf("[appdata] type=%d result_queue=FULL drop=1\n", static_cast<int>(raw->type));
      delete raw;
    }
  }

  void execute(std::unique_ptr<AppDataRequest> request) {
    if (!request) return;
    const uint32_t startedMs = millis();
    const uint32_t queueWaitMs = static_cast<uint32_t>(startedMs - request->createdMs);
    std::unique_ptr<AppDataResult> result(new (std::nothrow) AppDataResult());
    if (!result) return;
    result->requestId = request->requestId;
    result->type = request->type;
    result->entityIndex = request->entityIndex;

    if (request->type == AppDataRequestType::WEATHER) {
      result->error = weather.fetch(request->location, result->weather, &result->diagnostics);
      result->completedMs = millis();
      Serial.printf("[appdata] id=%lu type=WEATHER location=%s queue=%lums dur=%lums http=%d native=%d tls=%d bytes=%u/%ld result=%s\n",
                    static_cast<unsigned long>(request->requestId), request->location.displayName.c_str(),
                    static_cast<unsigned long>(queueWaitMs), static_cast<unsigned long>(result->diagnostics.elapsedMs),
                    result->diagnostics.httpStatus, result->diagnostics.nativeError, result->diagnostics.tlsError,
                    static_cast<unsigned>(result->diagnostics.receivedBytes),
                    static_cast<long>(result->diagnostics.expectedBytes), weatherErrorName(result->error));
    } else if (request->type == AppDataRequestType::CRYPTO) {
      result->cryptoError = crypto.fetch(result->crypto, &result->cryptoDiagnostics);
      result->completedMs = millis();
      Serial.printf("[appdata] id=%lu type=CRYPTO queue=%lums dur=%lums http=%d native=%d tls=%d bytes=%u/%ld result=%s\n",
                    static_cast<unsigned long>(request->requestId), static_cast<unsigned long>(queueWaitMs),
                    static_cast<unsigned long>(result->cryptoDiagnostics.elapsedMs), result->cryptoDiagnostics.httpStatus,
                    result->cryptoDiagnostics.nativeError, result->cryptoDiagnostics.tlsError,
                    static_cast<unsigned>(result->cryptoDiagnostics.receivedBytes),
                    static_cast<long>(result->cryptoDiagnostics.expectedBytes), cryptoErrorName(result->cryptoError));
    } else if (request->type == AppDataRequestType::HOME_ASSISTANT) {
      const HomeAssistantConfig* config = request->homeAssistantConfig;
      if (!config || request->entityIndex >= config->entityCount) {
        result->homeAssistantError = HomeAssistantError::MISSING_FIELD;
        result->completedMs = millis();
      } else {
        const HomeAssistantEntityConfig& entity = config->entities[request->entityIndex];
        result->homeAssistantError = homeAssistant.fetch(*config, entity, result->homeAssistant,
                                                         &result->homeAssistantDiagnostics);
        result->completedMs = millis();
        Serial.printf("[appdata] id=%lu type=HOME_ASSISTANT entity=%s queue=%lums dur=%lums http=%d native=%d tls=%d bytes=%u/%ld result=%s\n",
                      static_cast<unsigned long>(request->requestId), entity.entityId.c_str(),
                      static_cast<unsigned long>(queueWaitMs),
                      static_cast<unsigned long>(result->homeAssistantDiagnostics.elapsedMs),
                      result->homeAssistantDiagnostics.httpStatus, result->homeAssistantDiagnostics.nativeError,
                      result->homeAssistantDiagnostics.tlsError,
                      static_cast<unsigned>(result->homeAssistantDiagnostics.receivedBytes),
                      static_cast<long>(result->homeAssistantDiagnostics.expectedBytes),
                      homeAssistantErrorName(result->homeAssistantError));
      }
    }
    sendResult(std::move(result));
  }

  void run() {
    while (true) {
      AppDataRequest* incoming = nullptr;
      if (xQueueReceive(requestQueue, &incoming, portMAX_DELAY) == pdTRUE && incoming) {
        execute(std::unique_ptr<AppDataRequest>(incoming));
      }
    }
  }

  void drainResultQueue(QueueHandle_t queue) {
    if (!queue) return;
    AppDataResult* result = nullptr;
    while (xQueueReceive(queue, &result, 0) == pdTRUE) delete result;
  }

  void cleanup() {
    AppDataRequest* request = nullptr;
    if (requestQueue) {
      while (xQueueReceive(requestQueue, &request, 0) == pdTRUE) delete request;
      vQueueDelete(requestQueue);
      requestQueue = nullptr;
    }
    for (QueueHandle_t* queue : {&weatherResultQueue, &cryptoResultQueue, &homeAssistantResultQueue}) {
      if (*queue) {
        drainResultQueue(*queue);
        vQueueDelete(*queue);
        *queue = nullptr;
      }
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
  impl_->weatherResultQueue = xQueueCreate(Impl::RESULT_QUEUE_DEPTH, sizeof(AppDataResult*));
  impl_->cryptoResultQueue = xQueueCreate(Impl::RESULT_QUEUE_DEPTH, sizeof(AppDataResult*));
  impl_->homeAssistantResultQueue = xQueueCreate(Impl::RESULT_QUEUE_DEPTH, sizeof(AppDataResult*));
  if (!impl_->requestQueue || !impl_->weatherResultQueue || !impl_->cryptoResultQueue ||
      !impl_->homeAssistantResultQueue) {
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

bool AppDataWorker::tryReceive(AppDataRequestType type, AppDataResult& result) {
  if (!impl_->started) return false;
  QueueHandle_t queue = impl_->resultQueueFor(type);
  if (!queue) return false;
  AppDataResult* incoming = nullptr;
  if (xQueueReceive(queue, &incoming, 0) != pdTRUE || !incoming) return false;
  result = std::move(*incoming);
  delete incoming;
  return true;
}
