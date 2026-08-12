#include "HttpTransport.h"

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
  std::string takeBody() { return buffer_.takeBody(); }

 private:
  HttpBodyBuffer buffer_;
};

}  // namespace

HttpResponse HttpTransport::get(const std::string& url, const HttpHeaders& headers) {
  WiFiClientSecure client;
  // V1 uses public quote endpoints without certificate pinning. Encryption remains enabled,
  // but certificate identity is not verified; README documents this trade-off explicitly.
  client.setInsecure();
  client.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);

  HTTPClient http;
  http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);
  http.setUserAgent("TDisplayGP/1.0");
  http.setReuse(false);

  if (!http.begin(client, url.c_str())) {
    return {HttpTransportError::NETWORK, 0, {}};
  }
  for (const auto& header : headers) {
    http.addHeader(header.name.c_str(), header.value.c_str());
  }

  const int status = http.GET();
  if (status <= 0) {
    http.end();
    return {HttpTransportError::NETWORK, status, {}};
  }
  if (status != HTTP_CODE_OK) {
    http.end();
    return {HttpTransportError::HTTP_STATUS, status, {}};
  }

  const int contentLength = http.getSize();
  if (contentLength > static_cast<int>(BuildConfig::HTTP_MAX_BODY_BYTES)) {
    http.end();
    return {HttpTransportError::BODY_TOO_LARGE, status, {}};
  }

  BoundedMemoryStream sink(BuildConfig::HTTP_MAX_BODY_BYTES);
  const int written = http.writeToStream(&sink);
  http.end();

  if (sink.overflowed()) {
    return {HttpTransportError::BODY_TOO_LARGE, status, {}};
  }
  if (written < 0) {
    return {HttpTransportError::NETWORK, status, {}};
  }
  if (contentLength >= 0 && written != contentLength) {
    return {HttpTransportError::NETWORK, status, {}};
  }
  return {HttpTransportError::NONE, status, sink.takeBody()};
}
