#include "AppDataWorker.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <new>
#include <utility>

#include "HomeAssistantProvider.h"
#include "HttpTransport.h"
#include "WeatherProvider.h"

namespace {
constexpr UBaseType_t REQUEST_QUEUE_DEPTH = 6;
constexpr UBaseType_t RESULT_QUEUE_DEPTH = 4;
constexpr uint32_t TASK_STACK_WORDS = 8192;

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
}  // namespace

struct AppDataWorker::Impl {
  HttpTransport transport;
  OpenMeteoProvider weather{transport};
  SecureHomeAssistantTransport homeAssistantTransport;
  HomeAssistantProvider homeAssistant{homeAssistantTransport};
  QueueHandle_t requestQueue = nullptr;
  QueueHandle_t weatherResultQueue = nullptr;
  QueueHandle_t homeAssistantResultQueue = nullptr;
  TaskHandle_t task = nullptr;

  ~Impl() { cleanup(); }

  QueueHandle_t resultQueueFor(AppDataRequestType type) const {
    switch (type) {
      case AppDataRequestType::WEATHER: return weatherResultQueue;
      case AppDataRequestType::HOME_ASSISTANT: return homeAssistantResultQueue;
    }
    return nullptr;
  }

  void drainQueue(QueueHandle_t queue, bool requests) {
    if (!queue) return;
    void* ptr = nullptr;
    while (xQueueReceive(queue, &ptr, 0) == pdTRUE) {
      if (requests) delete static_cast<AppDataRequest*>(ptr);
      else delete static_cast<AppDataResult*>(ptr);
    }
  }

  void cleanup() {
    if (task) {
      vTaskDelete(task);
      task = nullptr;
    }
    drainQueue(requestQueue, true);
    drainQueue(weatherResultQueue, false);
    drainQueue(homeAssistantResultQueue, false);
    if (requestQueue) { vQueueDelete(requestQueue); requestQueue = nullptr; }
    if (weatherResultQueue) { vQueueDelete(weatherResultQueue); weatherResultQueue = nullptr; }
    if (homeAssistantResultQueue) { vQueueDelete(homeAssistantResultQueue); homeAssistantResultQueue = nullptr; }
  }

  void publishResult(AppDataResult* result) {
    if (!result) return;
    QueueHandle_t queue = resultQueueFor(result->type);
    if (!queue || xQueueSend(queue, &result, 0) != pdTRUE) {
      delete result;
    }
  }

  void execute(AppDataRequest* request) {
    if (!request) return;
    std::unique_ptr<AppDataRequest> ownedRequest(request);
    std::unique_ptr<AppDataResult> result(new (std::nothrow) AppDataResult());
    if (!result) return;
    result->requestId = request->requestId;
    result->type = request->type;

    if (request->type == AppDataRequestType::WEATHER) {
      result->error = weather.fetch(request->location, result->weather, &result->diagnostics);
      Serial.printf("[appdata] type=WEATHER id=%lu error=%s http=%d native=%d tls=%d elapsed=%lums\n",
                    static_cast<unsigned long>(request->requestId), weatherErrorName(result->error),
                    result->diagnostics.httpStatus, result->diagnostics.nativeError,
                    result->diagnostics.tlsError,
                    static_cast<unsigned long>(result->diagnostics.elapsedMs));
    } else if (request->type == AppDataRequestType::HOME_ASSISTANT) {
      if (!request->homeAssistantConfig ||
          request->entityIndex >= request->homeAssistantConfig->entityCount) {
        result->homeAssistantError = HomeAssistantError::MISSING_FIELD;
      } else {
        result->entityIndex = request->entityIndex;
        result->homeAssistantError = homeAssistant.fetch(
            *request->homeAssistantConfig,
            request->homeAssistantConfig->entities[request->entityIndex],
            result->homeAssistant,
            &result->homeAssistantDiagnostics);
      }
      Serial.printf("[appdata] type=HOME_ASSISTANT id=%lu entity=%u error=%s http=%d native=%d tls=%d elapsed=%lums\n",
                    static_cast<unsigned long>(request->requestId),
                    static_cast<unsigned>(request->entityIndex),
                    homeAssistantErrorName(result->homeAssistantError),
                    result->homeAssistantDiagnostics.httpStatus,
                    result->homeAssistantDiagnostics.nativeError,
                    result->homeAssistantDiagnostics.tlsError,
                    static_cast<unsigned long>(result->homeAssistantDiagnostics.elapsedMs));
    }

    result->completedMs = millis();
    publishResult(result.release());
  }

  static void taskEntry(void* arg) {
    auto* self = static_cast<Impl*>(arg);
    for (;;) {
      AppDataRequest* request = nullptr;
      if (xQueueReceive(self->requestQueue, &request, portMAX_DELAY) == pdTRUE) {
        self->execute(request);
      }
    }
  }
};

AppDataWorker::AppDataWorker() : impl_(new Impl()) {}
AppDataWorker::~AppDataWorker() = default;

bool AppDataWorker::begin() {
  if (impl_->task) return true;
  impl_->requestQueue = xQueueCreate(REQUEST_QUEUE_DEPTH, sizeof(AppDataRequest*));
  impl_->weatherResultQueue = xQueueCreate(RESULT_QUEUE_DEPTH, sizeof(AppDataResult*));
  impl_->homeAssistantResultQueue = xQueueCreate(RESULT_QUEUE_DEPTH, sizeof(AppDataResult*));
  if (!impl_->requestQueue || !impl_->weatherResultQueue || !impl_->homeAssistantResultQueue) {
    impl_->cleanup();
    return false;
  }
  if (xTaskCreatePinnedToCore(Impl::taskEntry, "appdata", TASK_STACK_WORDS, impl_.get(), 1,
                              &impl_->task, 0) != pdPASS) {
    impl_->cleanup();
    return false;
  }
  return true;
}

bool AppDataWorker::enqueue(const AppDataRequest& request) {
  if (!impl_->requestQueue) return false;
  AppDataRequest* copy = new (std::nothrow) AppDataRequest(request);
  if (!copy) return false;
  if (xQueueSend(impl_->requestQueue, &copy, 0) != pdTRUE) {
    delete copy;
    return false;
  }
  return true;
}

bool AppDataWorker::tryReceive(AppDataRequestType type, AppDataResult& result) {
  QueueHandle_t queue = impl_->resultQueueFor(type);
  if (!queue) return false;
  AppDataResult* received = nullptr;
  if (xQueueReceive(queue, &received, 0) != pdTRUE || !received) return false;
  result = std::move(*received);
  delete received;
  return true;
}
