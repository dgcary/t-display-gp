#include "BambuCloudClient.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include <string>
#include <utility>

#include "HttpTransport.h"
#include "NetworkArbiter.h"
#include "build_config.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {
constexpr char BAMBU_USER_AGENT[] = "bambu_network_agent/01.09.05.01";

struct CloudHttpResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  int statusCode = 0;
  std::string body;
};

class NetworkRequestGuard {
 public:
  explicit NetworkRequestGuard(NetworkArbiter& arbiter)
      : arbiter_(arbiter), locked_(arbiter_.lock()) {}
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
    return buffer_.append(&byte, 1U) ? 1U : 0U;
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

const char* apiHost(BambuRegion region) {
  return region == BambuRegion::CHINA ? "api.bambulab.cn" : "api.bambulab.com";
}

const char* siteHost(BambuRegion region) {
  return region == BambuRegion::CHINA ? "bambulab.cn" : "bambulab.com";
}

std::string httpsUrl(const char* host, const char* path) {
  return std::string("https://") + host + path;
}

int lastTlsError(WiFiClientSecure& client) {
  char buffer[96] = {};
  return client.lastError(buffer, sizeof(buffer));
}

BambuCloudError classifyNetworkFailure(WiFiClientSecure& client) {
  return lastTlsError(client) != 0 ? BambuCloudError::TLS : BambuCloudError::NETWORK;
}

CloudHttpResult executeRequest(const std::string& url, const std::string& bearerToken) {
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) return {};

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start);
  tls.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tls.setTimeout((BuildConfig::HTTP_READ_TIMEOUT_MS + 999U) / 1000U);

  HTTPClient http;
  http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);
  http.setUserAgent(BAMBU_USER_AGENT);
  http.setReuse(false);
  if (!http.begin(tls, url.c_str())) {
    CloudHttpResult result;
    result.error = classifyNetworkFailure(tls);
    return result;
  }
  http.addHeader("Accept", "application/json");
  http.addHeader("X-BBL-Client-Name", "OrcaSlicer");
  http.addHeader("X-BBL-Client-Type", "slicer");
  http.addHeader("X-BBL-Client-Version", "01.09.05.51");
  http.addHeader("X-BBL-Language", "en-US");
  http.addHeader("X-BBL-OS-Type", "linux");
  http.addHeader("X-BBL-OS-Version", "6.2.0");
  http.addHeader("X-BBL-Agent-Version", "01.09.05.01");
  const std::string authorization = "Bearer " + bearerToken;
  http.addHeader("Authorization", authorization.c_str());

  const int status = http.GET();
  if (status <= 0) {
    CloudHttpResult result;
    result.error = classifyNetworkFailure(tls);
    http.end();
    return result;
  }
  const int contentLength = http.getSize();
  if (contentLength > static_cast<int>(BuildConfig::BAMBU_HTTPS_MAX_BODY_BYTES)) {
    CloudHttpResult result;
    result.error = BambuCloudError::BODY_TOO_LARGE;
    result.statusCode = status;
    http.end();
    return result;
  }

  BoundedMemoryStream sink(BuildConfig::BAMBU_HTTPS_MAX_BODY_BYTES);
  const int written = http.writeToStream(&sink);
  const size_t received = sink.size();
  const bool overflowed = sink.overflowed();
  const BambuCloudError ioError = written < 0 ? classifyNetworkFailure(tls) : BambuCloudError::NONE;
  http.end();

  CloudHttpResult result;
  result.statusCode = status;
  if (overflowed) {
    result.error = BambuCloudError::BODY_TOO_LARGE;
    return result;
  }
  if (written < 0) {
    result.error = ioError;
    return result;
  }
  if (contentLength >= 0 && static_cast<size_t>(contentLength) != received) {
    result.error = BambuCloudError::TRUNCATED_BODY;
    return result;
  }
  result.error = status >= 200 && status < 300 ? BambuCloudError::NONE : BambuCloudError::HTTP_STATUS;
  result.body = sink.takeBody();
  return result;
}
}  // namespace

BambuCloudUserIdResult BambuCloudClient::fetchUserId(const std::string& token,
                                                      BambuRegion region) const {
  BambuCloudUserIdResult result;
  if (token.empty() || token.size() > BambuConfigLimits::ACCESS_TOKEN) {
    result.error = BambuCloudError::INVALID_CREDENTIALS;
    return result;
  }
  std::string userId;
  if (extractBambuUserIdFromJwt(token, userId)) {
    result.error = BambuCloudError::NONE;
    result.userId = std::move(userId);
    return result;
  }
  const CloudHttpResult http = executeRequest(
      httpsUrl(apiHost(region), "/v1/user-service/my/profile"), token);
  if (http.error != BambuCloudError::NONE) {
    result.error = http.error;
    return result;
  }
  if (!parseBambuProfileUserId(http.body, userId)) {
    result.error = BambuCloudError::USER_ID_UNAVAILABLE;
    return result;
  }
  result.error = BambuCloudError::NONE;
  result.userId = std::move(userId);
  return result;
}

BambuCloudPrintersResult BambuCloudClient::fetchPrinters(const std::string& token,
                                                          BambuRegion region) const {
  BambuCloudPrintersResult result;
  if (token.empty() || token.size() > BambuConfigLimits::ACCESS_TOKEN) {
    result.error = BambuCloudError::INVALID_CREDENTIALS;
    return result;
  }
  const CloudHttpResult http = executeRequest(
      httpsUrl(siteHost(region), "/api/v1/iot-service/api/user/bind"), token);
  if (http.error != BambuCloudError::NONE) {
    result.error = http.error;
    return result;
  }
  std::vector<BambuCloudDevice> printers;
  if (!parseBambuDeviceList(http.body, printers)) {
    result.error = BambuCloudError::MALFORMED;
    return result;
  }
  result.error = BambuCloudError::NONE;
  result.printers = std::move(printers);
  return result;
}
