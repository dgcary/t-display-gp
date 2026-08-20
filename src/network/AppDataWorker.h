#pragma once

#include <memory>

#include "AppDataTypes.h"

class AppDataWorker final : public IAppDataQueue {
 public:
  AppDataWorker();
  ~AppDataWorker();
  AppDataWorker(const AppDataWorker&) = delete;
  AppDataWorker& operator=(const AppDataWorker&) = delete;

  bool begin();
  bool enqueue(const AppDataRequest& request) override;
  bool tryReceive(AppDataRequestType type, AppDataResult& result) override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
