#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

struct HttpHeader {
  std::string name;
  std::string value;
};

using HttpHeaders = std::vector<HttpHeader>;

enum class HttpTransportError {
  NONE,
  NETWORK,
  HTTP_STATUS,
  BODY_TOO_LARGE
};

struct HttpResponse {
  HttpTransportError error = HttpTransportError::NONE;
  int statusCode = 0;
  std::string body;
};

class HttpBodyBuffer {
 public:
  explicit HttpBodyBuffer(size_t limit) : limit_(limit) { body_.reserve(limit); }

  bool append(const char* data, size_t length) {
    if (overflowed_) return false;
    const size_t remaining = limit_ - body_.size();
    const size_t accepted = length <= remaining ? length : remaining;
    body_.append(data, accepted);
    if (accepted != length) {
      overflowed_ = true;
      return false;
    }
    return true;
  }
  bool overflowed() const { return overflowed_; }
  const std::string& body() const { return body_; }
  std::string takeBody() { return std::move(body_); }

 private:
  size_t limit_;
  bool overflowed_ = false;
  std::string body_;
};

class IHttpTransport {
 public:
  virtual ~IHttpTransport() = default;
  virtual HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) = 0;
};

class HttpTransport final : public IHttpTransport {
 public:
  HttpResponse get(const std::string& url, const HttpHeaders& headers = {}) override;
};
