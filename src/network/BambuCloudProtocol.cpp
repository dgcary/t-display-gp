#include "BambuCloudProtocol.h"

bool parseBambuLoginReply(int, const std::string&, BambuLoginReply&) {
  return false;
}

bool extractBambuUserIdFromJwt(const std::string&, std::string&) {
  return false;
}

bool parseBambuDeviceList(const std::string&, std::vector<BambuCloudDevice>&) {
  return false;
}

std::string bambuReportTopic(const std::string&) {
  return {};
}
