#pragma once
#include "project_configs.hpp"

#include <memory>
#include <mutex>
#include <string>

#include "esp_log.h"
#include "my_mutex.hpp"
#include "task.hpp"

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

    static void Log(std::string tag, std::string msg) noexcept { ESP_LOGI(tag.c_str(), "%s", msg.c_str()); }
    static void LogError(std::string tag, std::string msg) noexcept { ESP_LOGE(tag.c_str(), "%s", msg.c_str()); }
    static void OnFailTermination(std::string tag, std::string msg) noexcept
    {
        LogError(tag, msg);

        for (int i = 5; i >= 0; i--) {
            LogError("TERMINATING", "...." + std::to_string(i) + "....");
            Task::DelayMs(500);
        }

        std::terminate();
    }

  protected:
  private:
    EspLogger() = default;

    std::shared_ptr<EspLogger> static _this;
    Mutex mutable writeMutex;
};

class Logger {
  public:
    Logger(std::string new_tag, ProjCfg::EnableLogForComponent component_logging_enabled)
      : tag{ new_tag }
      , console{ EspLogger::Get() }
      , isEnabled{ static_cast<bool>(component_logging_enabled) }
    { }

    void LogError(std::string text) noexcept
    {
        if (isEnabled or ProjCfg::Log::LogAllErrors)
            console->LogError(tag, text);
    }
    void Log(std::string text) const noexcept
    {
        if (isEnabled)
            console->Log(tag, text);
    }
    void OnFatalErrorTermination(std::string msg) noexcept {
        console->OnFailTermination(tag, msg);
    }
    void SetNewTag(std::string new_tag) noexcept { tag = new_tag; }

  private:
    std::string                tag     = "";
    std::shared_ptr<EspLogger> console = nullptr;
    bool                       isEnabled{ false };
};