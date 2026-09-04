#pragma once

#include "BambuConfig.h"

class BambuConfigStore {
 public:
  bool load(BambuConfig& out) const;
  bool save(const BambuConfig& config) const;
  bool clear() const;
};
