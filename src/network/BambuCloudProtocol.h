#pragma once

#include <string>
#include <vector>

enum class BambuLoginDisposition {
  TOKEN,
  NEED_VERIFICATION_CODE,
  NEED_TFA,
  ERROR,
};

enum class BambuVerificationChannel {
  EMAIL,
  SMS,
};

struct BambuLoginReply {
  BambuLoginDisposition disposition = BambuLoginDisposition::ERROR;
  std::string accessToken;
  std::string tfaKey;
  std::string error;
};

struct BambuCloudDevice {
  std::string serial;
  std::string name;
  std::string model;
};

bool parseBambuLoginReply(int httpStatus, const std::string& body, BambuLoginReply& out);
BambuVerificationChannel bambuVerificationChannelForAccount(const std::string& account);
bool extractBambuUserIdFromJwt(const std::string& token, std::string& userId);
bool parseBambuProfileUserId(const std::string& body, std::string& userId);
bool parseBambuDeviceList(const std::string& body, std::vector<BambuCloudDevice>& out);
std::string bambuReportTopic(const std::string& serial);
