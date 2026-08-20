#include "HomeAssistantProvider.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "NetworkArbiter.h"
#include "build_config.h"

namespace {
constexpr size_t HA_MAX_BODY_BYTES = 4096;

class NetworkRequestGuard {
 public:
  explicit NetworkRequestGuard(NetworkArbiter& arbiter) : arbiter_(arbiter), locked_(arbiter_.lock()) {}
  ~NetworkRequestGuard() { if (locked_) arbiter_.unlock(); }
  bool locked() const { return locked_; }
 private:
  NetworkArbiter& arbiter_;
  bool locked_ = false;
};

class BoundedMemoryStream final : public Stream {
 public:
  explicit BoundedMemoryStream(size_t limit) : buffer_(limit) {}
  size_t write(uint8_t value) override {
    const char byte = static_cast<char>(value);
    return buffer_.append(&byte, 1) ? 1U : 0U;
  }
  size_t write(const uint8_t* data, size_t length) override {
    const size_t before = buffer_.body().size();
    buffer_.append(reinterpret_cast<const char*>(data), length);
    return buffer_.body().size() - before;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
  bool overflowed() const { return buffer_.overflowed(); }
  size_t size() const { return buffer_.body().size(); }
  std::string takeBody() { return buffer_.takeBody(); }
 private:
  HttpBodyBuffer buffer_;
};

int lastTlsError(WiFiClientSecure& client) {
  char buffer[96] = {};
  return client.lastError(buffer, sizeof(buffer));
}

HttpResponse makeResponse(HttpTransportError error, int statusCode, std::string body,
                          int nativeError, int tlsError, int32_t expectedBytes,
                          size_t receivedBytes, uint32_t startedMs) {
  HttpResponse response;
  response.error = error;
  response.statusCode = statusCode;
  response.body = std::move(body);
  response.nativeError = nativeError;
  response.tlsError = tlsError;
  response.expectedBytes = expectedBytes;
  response.receivedBytes = receivedBytes;
  response.elapsedMs = static_cast<uint32_t>(millis() - startedMs);
  return response;
}

std::string hostFromUrl(const std::string& url) {
  const size_t start = url.find("://");
  const size_t hostStart = start == std::string::npos ? 0U : start + 3U;
  const size_t hostEnd = url.find_first_of("/:?", hostStart);
  return hostEnd == std::string::npos ? url.substr(hostStart) : url.substr(hostStart, hostEnd - hostStart);
}

void logDiagnostic(const std::string& url, const HttpResponse& response,
                   uint32_t arbiterWaitMs, uint32_t ioElapsedMs) {
  const std::string host = hostFromUrl(url);
  const String ip = WiFi.localIP().toString();
  const String bssid = WiFi.BSSIDstr();
  Serial.printf("[net] host=%s mode=HA_CA arb=%lums io=%lums total=%lums wifi=%d rssi=%ld ip=%s bssid=%s ch=%d http=%d native=%d tls=%d result=%d\n",
                host.c_str(), static_cast<unsigned long>(arbiterWaitMs),
                static_cast<unsigned long>(ioElapsedMs), static_cast<unsigned long>(response.elapsedMs),
                static_cast<int>(WiFi.status()), static_cast<long>(WiFi.RSSI()), ip.c_str(), bssid.c_str(),
                static_cast<int>(WiFi.channel()), response.statusCode, response.nativeError, response.tlsError,
                static_cast<int>(response.error));
}

HttpResponse finish(HttpResponse response, const std::string& url, uint32_t arbiterWaitMs,
                    uint32_t ioStartedMs) {
  logDiagnostic(url, response, arbiterWaitMs, static_cast<uint32_t>(millis() - ioStartedMs));
  return response;
}
}  // namespace

HttpResponse SecureHomeAssistantTransport::get(const HomeAssistantConfig& config,
                                               const HomeAssistantEntityConfig& entity) {
  const uint32_t startedMs = millis();
  const std::string url = config.baseUrl + "/api/states/" + entity.entityId;
  NetworkRequestGuard guard(sharedNetworkArbiter());
  const uint32_t ioStartedMs = millis();
  const uint32_t arbiterWaitMs = static_cast<uint32_t>(ioStartedMs - startedMs);
  if (!guard.locked()) {
    HttpResponse response = makeResponse(HttpTransportError::NETWORK, 0, {}, 0, 0, -1, 0, startedMs);
    logDiagnostic(url, response, arbiterWaitMs, 0);
    return response;
  }

  WiFiClientSecure client;
  client.setCACert(config.caCert.c_str());
  client.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);

  HTTPClient http;
  http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);
  http.setUserAgent("TDisplayGP/1.0");
  http.setReuse(false);
  if (!http.begin(client, url.c_str())) {
    return finish(makeResponse(HttpTransportError::NETWORK, 0, {}, 0, lastTlsError(client), -1, 0, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }

  const std::string authorization = "Bearer " + config.token;
  http.addHeader("Authorization", authorization.c_str());
  http.addHeader("Accept", "application/json");
  const int status = http.GET();
  if (status <= 0) {
    const int tlsError = lastTlsError(client);
    http.end();
    return finish(makeResponse(HttpTransportError::NETWORK, 0, {}, status, tlsError, -1, 0, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }

  const int contentLength = http.getSize();
  if (status != HTTP_CODE_OK) {
    http.end();
    return finish(makeResponse(HttpTransportError::HTTP_STATUS, status, {}, 0, 0, contentLength, 0, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }
  if (contentLength > static_cast<int>(HA_MAX_BODY_BYTES)) {
    http.end();
    return finish(makeResponse(HttpTransportError::BODY_TOO_LARGE, status, {}, 0, 0, contentLength, 0, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }

  BoundedMemoryStream sink(HA_MAX_BODY_BYTES);
  const int written = http.writeToStream(&sink);
  const size_t receivedBytes = sink.size();
  const int tlsError = written < 0 ? lastTlsError(client) : 0;
  http.end();
  if (sink.overflowed()) {
    return finish(makeResponse(HttpTransportError::BODY_TOO_LARGE, status, {}, written < 0 ? written : 0,
                               tlsError, contentLength, receivedBytes, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }
  if (written < 0) {
    return finish(makeResponse(HttpTransportError::NETWORK, status, {}, written, tlsError,
                               contentLength, receivedBytes, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }
  if (contentLength >= 0 && written != contentLength) {
    return finish(makeResponse(HttpTransportError::TRUNCATED_BODY, status, {}, 0, tlsError,
                               contentLength, receivedBytes, startedMs),
                  url, arbiterWaitMs, ioStartedMs);
  }
  return finish(makeResponse(HttpTransportError::NONE, status, sink.takeBody(), 0, 0,
                             contentLength, receivedBytes, startedMs),
                url, arbiterWaitMs, ioStartedMs);
}
