#include "HttpTransport.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "build_config.h"

namespace {

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

}  // namespace

HttpResponse HttpTransport::get(const std::string& url, const HttpHeaders& headers) {
  const uint32_t startedMs = millis();
  WiFiClientSecure client;
  // V1 keeps the existing TLS identity behavior in this stability change.
  // Bound handshake time explicitly: Arduino-ESP32 2.0.14 defaults to 120 s.
  client.setInsecure();
  client.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);

  HTTPClient http;
  http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);
  http.setUserAgent("TDisplayGP/1.0");
  http.setReuse(false);

  if (!http.begin(client, url.c_str())) {
    return makeResponse(HttpTransportError::NETWORK, 0, {}, 0, lastTlsError(client),
                        -1, 0, startedMs);
  }
  for (const auto& header : headers) {
    http.addHeader(header.name.c_str(), header.value.c_str());
  }

  const int status = http.GET();
  if (status <= 0) {
    const int tlsError = lastTlsError(client);
    http.end();
    return makeResponse(HttpTransportError::NETWORK, 0, {}, status, tlsError,
                        -1, 0, startedMs);
  }

  const int contentLength = http.getSize();
  if (status != HTTP_CODE_OK) {
    http.end();
    return makeResponse(HttpTransportError::HTTP_STATUS, status, {}, 0, 0,
                        contentLength, 0, startedMs);
  }

  if (contentLength > static_cast<int>(BuildConfig::HTTP_MAX_BODY_BYTES)) {
    http.end();
    return makeResponse(HttpTransportError::BODY_TOO_LARGE, status, {}, 0, 0,
                        contentLength, 0, startedMs);
  }

  BoundedMemoryStream sink(BuildConfig::HTTP_MAX_BODY_BYTES);
  const int written = http.writeToStream(&sink);
  const size_t receivedBytes = sink.size();
  const int tlsError = written < 0 ? lastTlsError(client) : 0;
  http.end();

  if (sink.overflowed()) {
    return makeResponse(HttpTransportError::BODY_TOO_LARGE, status, {}, written < 0 ? written : 0,
                        tlsError, contentLength, receivedBytes, startedMs);
  }
  if (written < 0) {
    return makeResponse(HttpTransportError::NETWORK, status, {}, written, tlsError,
                        contentLength, receivedBytes, startedMs);
  }
  if (contentLength >= 0 && written != contentLength) {
    return makeResponse(HttpTransportError::TRUNCATED_BODY, status, {}, 0, tlsError,
                        contentLength, receivedBytes, startedMs);
  }
  return makeResponse(HttpTransportError::NONE, status, sink.takeBody(), 0, 0,
                      contentLength, receivedBytes, startedMs);
}
