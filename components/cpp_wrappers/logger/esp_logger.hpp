#pragma once
#include <memory>
#include <mutex>
#include <string>

#include "esp_log.h"
#include "my_mutex.hpp"

class EspLogger {
  public:
    std::shared_ptr<EspLogger> static Get() noexcept
    {
        if (_this)
            return _this;
        else {
            Init();
            return _this;
        }
    }

    void static Init() { _this = std::shared_ptr<EspLogger>{ new EspLogger{} }; }

    void Log(std::string msg) const noexcept
    {
        std::lock_guard<Mutex>{ writeMutex };
        ESP_LOGI("", "%s", msg.c_str());
    }
    void LogError(std::string msg) const noexcept
    {
        std::lock_guard<Mutex>{ writeMutex };
        ESP_LOGE("", "%s", msg.c_str());
    }

  protected:
  private:
    EspLogger() = default;

    std::shared_ptr<EspLogger> static _this;
    Mutex mutable writeMutex;
};