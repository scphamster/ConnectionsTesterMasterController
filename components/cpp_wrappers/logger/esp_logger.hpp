#pragma once
#include <memory>
#include <string>

#include "esp_log.h"

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

    void static Init() noexcept { _this = std::shared_ptr<EspLogger>{ new EspLogger }; }

    void Log(std::string msg) const noexcept { ESP_LOGI("", "%s", msg.c_str()); }
    void LogError(std::string msg) const noexcept { ESP_LOGE("", "%s", msg.c_str()); }

  protected:
  private:
    EspLogger() noexcept = default;

    std::shared_ptr<EspLogger> static _this;
};