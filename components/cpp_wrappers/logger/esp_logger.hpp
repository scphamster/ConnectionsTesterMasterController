#pragma once
#include "project_configs.hpp"

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

class SmartLogger {
  public:
    SmartLogger(std::string new_tag, ProjCfg::EnableLogForComponent component_logging_enabled)
      : tag{ new_tag }
      , console{ EspLogger::Get() }
      , isEnabled{ static_cast<bool>(component_logging_enabled) }
    { }

    void LogError(std::string text) noexcept
    {
        if (isEnabled or ProjCfg::Log::LogAllErrors)
            console->LogError(tag + ": " + text);
    }
    void Log(std::string text) noexcept
    {
        if (isEnabled)
            console->Log(tag + ": " + text);
    }
    void SetNewTag(std::string new_tag) noexcept { tag = new_tag; }

  private:
    std::string                tag     = "";
    std::shared_ptr<EspLogger> console = nullptr;
    bool                       isEnabled{ false };
};