#include "BambuCloudClient.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <utility>

#include "HttpTransport.h"
#include "NetworkArbiter.h"
#include "build_config.h"

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

namespace {
constexpr char BAMBU_USER_AGENT[] = "bambu_network_agent/01.09.05.01";
constexpr char BAMBU_ACCEPT[] = "application/json";
constexpr char BAMBU_CONTENT_TYPE[] = "application/json";
constexpr uint32_t RAW_HTTP_TIMEOUT_MS = 12000U;

struct CloudHttpResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  int statusCode = 0;
  std::string body;
};

struct RawCloudResult {
  BambuCloudError error = BambuCloudError::NETWORK;
  int statusCode = 0;
  std::string body;
  std::string setCookies;
  bool truncated = false;
};

class NetworkRequestGuard {
 public:
  explicit NetworkRequestGuard(NetworkArbiter& arbiter)
      : arbiter_(arbiter), locked_(arbiter_.lock()) {}
  ~NetworkRequestGuard() {
    if (locked_) arbiter_.unlock();
  }
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

void secureErase(std::string& value) {
  if (!value.empty()) {
    volatile char* data = &value[0];
    for (size_t i = 0; i < value.size(); ++i) data[i] = '\0';
  }
  value.clear();
}

void configureHttp(HTTPClient& http) {
  http.setConnectTimeout(BuildConfig::HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(BuildConfig::HTTP_READ_TIMEOUT_MS);
  http.setUserAgent(BAMBU_USER_AGENT);
  http.setReuse(false);
}

CloudHttpResult executeRequest(const char* method,
                               const std::string& url,
                               const std::string* requestBody,
                               const std::string* bearerToken) {
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) return {};

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start);
  tls.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tls.setTimeout((BuildConfig::HTTP_READ_TIMEOUT_MS + 999U) / 1000U);

  HTTPClient http;
  configureHttp(http);
  if (!http.begin(tls, url.c_str())) {
    CloudHttpResult result;
    result.error = classifyNetworkFailure(tls);
    return result;
  }

  http.addHeader("Accept", BAMBU_ACCEPT);
  http.addHeader("Content-Type", BAMBU_CONTENT_TYPE);
  http.addHeader("X-BBL-Client-Name", "OrcaSlicer");
  http.addHeader("X-BBL-Client-Type", "slicer");
  http.addHeader("X-BBL-Client-Version", "01.09.05.51");
  http.addHeader("X-BBL-Language", "en-US");
  http.addHeader("X-BBL-OS-Type", "linux");
  http.addHeader("X-BBL-OS-Version", "6.2.0");
  http.addHeader("X-BBL-Agent-Version", "01.09.05.01");
  if (bearerToken && !bearerToken->empty()) {
    const std::string authorization = "Bearer " + *bearerToken;
    http.addHeader("Authorization", authorization.c_str());
  }

  int status = 0;
  if (std::string(method) == "GET") {
    status = http.GET();
  } else {
    status = http.POST(requestBody ? requestBody->c_str() : "");
  }

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
  const bool overflowed = sink.overflowed();
  const size_t received = sink.size();
  const BambuCloudError ioError = written < 0 ? classifyNetworkFailure(tls)
                                               : BambuCloudError::NONE;
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

  result.error = status >= 200 && status < 300
                     ? BambuCloudError::NONE
                     : BambuCloudError::HTTP_STATUS;
  result.body = sink.takeBody();
  return result;
}

bool appendBounded(std::string& body, const char* data, size_t length, bool& truncated) {
  if (body.size() + length > BuildConfig::BAMBU_HTTPS_MAX_BODY_BYTES) {
    const size_t room = BuildConfig::BAMBU_HTTPS_MAX_BODY_BYTES - body.size();
    if (room > 0U) body.append(data, room);
    truncated = true;
    return false;
  }
  body.append(data, length);
  return true;
}

std::string trimCopy(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
  return value;
}

void cookieJarPut(std::string& jar, const std::string& rawPair) {
  const std::string pair = trimCopy(rawPair);
  const size_t eq = pair.find('=');
  if (eq == std::string::npos || eq == 0U || eq + 1U >= pair.size()) return;
  const std::string needle = pair.substr(0, eq + 1U);

  size_t at = jar.find(needle);
  while (at != std::string::npos && at > 0U && jar[at - 1U] != ' ') {
    at = jar.find(needle, at + 1U);
  }
  if (at != std::string::npos) {
    size_t end = jar.find(';', at);
    if (end == std::string::npos) end = jar.size();
    else {
      ++end;
      if (end < jar.size() && jar[end] == ' ') ++end;
    }
    jar.erase(at, end - at);
  }
  if (!jar.empty() && jar.back() != ' ') jar += "; ";
  jar += pair;
}

void absorbSetCookies(std::string& jar, const std::string& setCookieLines) {
  size_t pos = 0U;
  while (pos < setCookieLines.size()) {
    size_t nl = setCookieLines.find('\n', pos);
    if (nl == std::string::npos) nl = setCookieLines.size();
    std::string line = trimCopy(setCookieLines.substr(pos, nl - pos));
    const size_t semi = line.find(';');
    if (semi != std::string::npos) line.resize(semi);
    cookieJarPut(jar, line);
    pos = nl + 1U;
  }
}

std::string cookieValue(const std::string& jar, const char* name) {
  const std::string needle = std::string(name) + "=";
  size_t at = jar.find(needle);
  while (at != std::string::npos && at > 0U && jar[at - 1U] != ' ') {
    at = jar.find(needle, at + 1U);
  }
  if (at == std::string::npos) return {};
  const size_t start = at + needle.size();
  size_t end = jar.find(';', start);
  if (end == std::string::npos) end = jar.size();
  return jar.substr(start, end - start);
}

RawCloudResult executeRawRequest(const char* host,
                                 const char* path,
                                 const char* method,
                                 const std::string* requestBody,
                                 const std::string* cookieJar,
                                 const std::string* csrfToken) {
  RawCloudResult out;
  NetworkRequestGuard guard(sharedNetworkArbiter());
  if (!guard.locked()) return out;

  WiFiClientSecure tls;
  tls.setCACertBundle(rootca_crt_bundle_start);
  tls.setHandshakeTimeout(BuildConfig::HTTP_TLS_HANDSHAKE_TIMEOUT_SEC);
  tls.setTimeout(5);

  esp_task_wdt_reset();
  if (!tls.connect(host, 443)) {
    out.error = classifyNetworkFailure(tls);
    return out;
  }
  esp_task_wdt_reset();

  std::string request = std::string(method) + " " + path + " HTTP/1.1\r\n";
  request += "Host: ";
  request += host;
  request += "\r\nUser-Agent: ";
  request += BAMBU_USER_AGENT;
  request += "\r\nAccept: application/json\r\nContent-Type: application/json\r\n";
  if (cookieJar && !cookieJar->empty()) {
    request += "Cookie: ";
    request += *cookieJar;
    request += "\r\n";
  }
  if (csrfToken && !csrfToken->empty()) {
    request += "x-bbl-csrf-token: ";
    request += *csrfToken;
    request += "\r\n";
  }
  request += "Connection: close\r\n";
  if (requestBody) {
    request += "Content-Length: ";
    request += std::to_string(requestBody->size());
    request += "\r\n";
  }
  request += "\r\n";
  if (requestBody) request += *requestBody;
  tls.print(request.c_str());
  secureErase(request);

  long contentLength = -1;
  bool chunked = false;
  const uint32_t started = millis();
  while (static_cast<uint32_t>(millis() - started) < RAW_HTTP_TIMEOUT_MS) {
    esp_task_wdt_reset();
    if (!tls.connected() && !tls.available()) break;
    if (!tls.available()) {
      delay(5);
      continue;
    }
    String line = tls.readStringUntil('\n');
    line.trim();
    String lower = line;
    lower.toLowerCase();
    if (out.statusCode == 0 && lower.startsWith("http/")) {
      const int sp = line.indexOf(' ');
      if (sp > 0) out.statusCode = line.substring(sp + 1, sp + 4).toInt();
    } else if (lower.startsWith("set-cookie:")) {
      out.setCookies.append(line.substring(11).c_str());
      out.setCookies.push_back('\n');
    } else if (lower.startsWith("content-length:")) {
      contentLength = line.substring(15).toInt();
    } else if (lower.startsWith("transfer-encoding:") && lower.indexOf("chunked") >= 0) {
      chunked = true;
    } else if (line.length() == 0U) {
      break;
    }
  }

  if (contentLength > static_cast<long>(BuildConfig::BAMBU_HTTPS_MAX_BODY_BYTES)) {
    tls.stop();
    out.error = BambuCloudError::BODY_TOO_LARGE;
    return out;
  }

  if (chunked) {
    bool desynced = false;
    while (!desynced && static_cast<uint32_t>(millis() - started) < RAW_HTTP_TIMEOUT_MS) {
      esp_task_wdt_reset();
      if (!tls.available()) {
        if (!tls.connected()) break;
        delay(5);
        continue;
      }
      String sizeLine = tls.readStringUntil('\n');
      sizeLine.trim();
      const long chunkSizeParsed = std::strtol(sizeLine.c_str(), nullptr, 16);
      if (chunkSizeParsed <= 0) break;
      long remaining = chunkSizeParsed;
      while (remaining > 0 && static_cast<uint32_t>(millis() - started) < RAW_HTTP_TIMEOUT_MS) {
        char buffer[129];
        const size_t want = remaining < 128 ? static_cast<size_t>(remaining) : 128U;
        const size_t got = tls.readBytes(buffer, want);
        if (got == 0U) {
          desynced = true;
          break;
        }
        if (!appendBounded(out.body, buffer, got, out.truncated)) {
          desynced = true;
          break;
        }
        remaining -= static_cast<long>(got);
      }
      if (!desynced) tls.readStringUntil('\n');
    }
    if (desynced && !out.truncated) out.error = BambuCloudError::TRUNCATED_BODY;
  } else if (contentLength >= 0) {
    long remaining = contentLength;
    while (remaining > 0 && static_cast<uint32_t>(millis() - started) < RAW_HTTP_TIMEOUT_MS) {
      esp_task_wdt_reset();
      char buffer[129];
      const size_t want = remaining < 128 ? static_cast<size_t>(remaining) : 128U;
      const size_t got = tls.readBytes(buffer, want);
      if (got == 0U) break;
      if (!appendBounded(out.body, buffer, got, out.truncated)) break;
      remaining -= static_cast<long>(got);
    }
    if (remaining > 0 && !out.truncated) out.error = BambuCloudError::TRUNCATED_BODY;
  } else {
    while (static_cast<uint32_t>(millis() - started) < RAW_HTTP_TIMEOUT_MS) {
      esp_task_wdt_reset();
      if (!tls.available()) {
        if (!tls.connected()) break;
        delay(5);
        continue;
      }
      char buffer[129];
      const size_t got = tls.readBytes(buffer, 128U);
      if (got == 0U) continue;
      if (!appendBounded(out.body, buffer, got, out.truncated)) break;
    }
  }

  tls.stop();
  esp_task_wdt_reset();
  if (out.truncated) {
    out.error = BambuCloudError::BODY_TOO_LARGE;
    return out;
  }
  if (out.error == BambuCloudError::TRUNCATED_BODY) return out;
  if (out.statusCode <= 0) {
    out.error = BambuCloudError::NETWORK;
    return out;
  }
  out.error = out.statusCode >= 200 && out.statusCode < 300
                  ? BambuCloudError::NONE
                  : BambuCloudError::HTTP_STATUS;
  return out;
}

std::string loginPayload(const std::string& email, const std::string& password) {
  DynamicJsonDocument doc(768);
  doc["account"] = email;
  doc["password"] = password;
  std::string body;
  serializeJson(doc, body);
  return body;
}

std::string verificationPayload(const std::string& email, const std::string& code) {
  DynamicJsonDocument doc(768);
  doc["account"] = email;
  doc["code"] = code;
  std::string body;
  serializeJson(doc, body);
  return body;
}

BambuCloudLoginResult parseLoginHttpResult(const CloudHttpResult& http,
                                           const std::string& account) {
  BambuCloudLoginResult result;
  result.httpStatus = http.statusCode;
  if (http.error != BambuCloudError::NONE && http.error != BambuCloudError::HTTP_STATUS) {
    result.error = http.error;
    return result;
  }

  BambuLoginReply reply;
  if (!parseBambuLoginReply(http.statusCode, http.body, reply)) {
    result.error = http.error == BambuCloudError::HTTP_STATUS
                       ? BambuCloudError::HTTP_STATUS
                       : BambuCloudError::MALFORMED;
    return result;
  }

  switch (reply.disposition) {
    case BambuLoginDisposition::TOKEN:
      result.error = BambuCloudError::NONE;
      result.accessToken = std::move(reply.accessToken);
      return result;
    case BambuLoginDisposition::NEED_VERIFICATION_CODE:
      result.error = BambuCloudError::VERIFICATION_REQUIRED;
      result.verificationType = bambuVerificationChannelForAccount(account) == BambuVerificationChannel::SMS
                                    ? BambuVerificationType::SMS_CODE
                                    : BambuVerificationType::EMAIL_CODE;
      return result;
    case BambuLoginDisposition::NEED_TFA:
      result.error = BambuCloudError::VERIFICATION_REQUIRED;
      result.verificationType = BambuVerificationType::TFA;
      result.tfaKey = std::move(reply.tfaKey);
      return result;
    case BambuLoginDisposition::ERROR:
      result.error = BambuCloudError::INVALID_CREDENTIALS;
      return result;
  }
  result.error = BambuCloudError::MALFORMED;
  return result;
}

bool validCode(const std::string& code) {
  if (code.size() < 4U || code.size() > 12U) return false;
  return std::all_of(code.begin(), code.end(), [](unsigned char c) { return std::isalnum(c) != 0; });
}
}  // namespace

BambuCloudLoginResult BambuCloudClient::login(const std::string& email,
                                               const std::string& password,
                                               BambuRegion region) const {
  BambuCloudLoginResult result;
  if (email.empty() || email.size() > BambuConfigLimits::EMAIL ||
      password.empty() || password.size() > BambuConfigLimits::PASSWORD) {
    result.error = BambuCloudError::INVALID_CREDENTIALS;
    return result;
  }

  std::string body = loginPayload(email, password);
  const CloudHttpResult http = executeRequest(
      "POST", httpsUrl(apiHost(region), "/v1/user-service/user/login"), &body, nullptr);
  secureErase(body);
  return parseLoginHttpResult(http, email);
}

BambuCloudLoginResult BambuCloudClient::submitVerificationCode(const std::string& email,
                                                                const std::string& code,
                                                                BambuRegion region) const {
  BambuCloudLoginResult result;
  if (email.empty() || email.size() > BambuConfigLimits::EMAIL || !validCode(code)) {
    result.error = BambuCloudError::INVALID_CREDENTIALS;
    return result;
  }

  std::string body = verificationPayload(email, code);
  const CloudHttpResult http = executeRequest(
      "POST", httpsUrl(apiHost(region), "/v1/user-service/user/login"), &body, nullptr);
  secureErase(body);
  return parseLoginHttpResult(http, email);
}

BambuCloudError BambuCloudClient::requestVerificationCode(const std::string& email,
                                                           BambuRegion region) const {
  if (email.empty() || email.size() > BambuConfigLimits::EMAIL) {
    return BambuCloudError::INVALID_CREDENTIALS;
  }

  const BambuVerificationChannel channel = bambuVerificationChannelForAccount(email);
  DynamicJsonDocument doc(768);
  std::string url;
  if (channel == BambuVerificationChannel::EMAIL) {
    doc["email"] = email;
    doc["type"] = "codeLogin";
    url = httpsUrl(apiHost(region), "/v1/user-service/user/sendemail/code");
  } else {
    if (region != BambuRegion::CHINA) return BambuCloudError::INVALID_CREDENTIALS;
    doc["phone"] = email;
    doc["type"] = "codeLogin";
    url = httpsUrl(siteHost(region), "/api/v1/user-service/user/sendsmscode");
  }

  std::string body;
  serializeJson(doc, body);
  const CloudHttpResult http = executeRequest("POST", url, &body, nullptr);
  secureErase(body);
  return http.error;
}

BambuCloudLoginResult BambuCloudClient::submitTfaCode(const std::string& tfaKey,
                                                       const std::string& code,
                                                       BambuRegion region) const {
  BambuCloudLoginResult result;
  result.verificationType = BambuVerificationType::TFA;
  if (tfaKey.empty() || tfaKey.size() > 512U || !validCode(code)) {
    result.error = BambuCloudError::INVALID_CREDENTIALS;
    return result;
  }

  std::string cookieJar;
  const RawCloudResult csrf = executeRawRequest(
      siteHost(region), "/api/csrf", "GET", nullptr, nullptr, nullptr);
  result.httpStatus = csrf.statusCode;
  if (csrf.error != BambuCloudError::NONE) {
    result.error = csrf.error;
    return result;
  }
  absorbSetCookies(cookieJar, csrf.setCookies);
  const std::string csrfToken = cookieValue(cookieJar, "bbl_csrf_token");
  if (csrfToken.empty()) {
    result.error = BambuCloudError::MALFORMED;
    return result;
  }

  DynamicJsonDocument doc(1024);
  doc["tfaKey"] = tfaKey;
  doc["tfaCode"] = code;
  std::string body;
  serializeJson(doc, body);
  const RawCloudResult tfa = executeRawRequest(
      siteHost(region), "/api/sign-in/tfa", "POST", &body, &cookieJar, &csrfToken);
  secureErase(body);
  result.httpStatus = tfa.statusCode;
  if (tfa.error != BambuCloudError::NONE) {
    result.error = tfa.error == BambuCloudError::HTTP_STATUS
                       ? BambuCloudError::INVALID_CREDENTIALS
                       : tfa.error;
    return result;
  }

  BambuLoginReply reply;
  if (!tfa.body.empty() && parseBambuLoginReply(200, tfa.body, reply) &&
      reply.disposition == BambuLoginDisposition::TOKEN) {
    result.error = BambuCloudError::NONE;
    result.accessToken = std::move(reply.accessToken);
    result.verificationType = BambuVerificationType::NONE;
    return result;
  }

  absorbSetCookies(cookieJar, tfa.setCookies);
  std::string token = cookieValue(cookieJar, "token");
  if (token.empty() || token.size() > BambuConfigLimits::ACCESS_TOKEN) {
    result.error = BambuCloudError::MALFORMED;
    return result;
  }
  result.error = BambuCloudError::NONE;
  result.accessToken = std::move(token);
  result.verificationType = BambuVerificationType::NONE;
  return result;
}

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
      "GET", httpsUrl(apiHost(region), "/v1/user-service/my/profile"), nullptr, &token);
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
      "GET", httpsUrl(siteHost(region), "/api/v1/iot-service/api/user/bind"), nullptr, &token);
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
