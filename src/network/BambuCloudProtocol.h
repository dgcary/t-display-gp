#pragma once

#include <string>
#include <vector>

struct BambuCloudDevice {
  std::string serial;
  std::string name;
  std::string model;
};

bool extractBambuUserIdFromJwt(const std::string& token, std::string& userId);
bool parseBambuProfileUserId(const std::string& body, std::string& userId);
bool parseBambuDeviceList(const std::string& body, std::vector<BambuCloudDevice>& out);
std::string bambuReportTopic(const std::string& serial);
