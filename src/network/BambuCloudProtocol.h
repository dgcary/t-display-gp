#pragma once

#include <string>
#include <vector>

enum class BambuLoginDisposition {
  TOKEN,
  NEED_EMAIL_CODE,
  NEED_TFA,
  ERROR,
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
bool extractBambuUserIdFromJwt(const std::string& token, std::string& userId);
bool parseBambuDeviceList(const std::string& body, std::vector<BambuCloudDevice>& out);
std::string bambuReportTopic(const std::string& serial);
