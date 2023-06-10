#pragma once

#include "esp_logger.hpp"

class ResetReasonNotifier {
  public:
    enum class RR {
        UNKNOWN   = ESP_RST_UNKNOWN,     //!< Reset reason can not be determined
        POWERON   = ESP_RST_POWERON,     //!< Reset due to power-on event
        EXT       = ESP_RST_EXT,         //!< Reset by external pin (not applicable for ESP32)
        SW        = ESP_RST_SW,          //!< Software reset via esp_restart
        PANIC     = ESP_RST_PANIC,       //!< Software reset due to exception/panic
        INT_WDT   = ESP_RST_INT_WDT,     //!< Reset (software or hardware) due to interrupt watchdog
        TASK_WDT  = ESP_RST_TASK_WDT,    //!< Reset due to task watchdog
        WDT       = ESP_RST_WDT,         //!< Reset due to other watchdogs
        DEEPSLEEP = ESP_RST_DEEPSLEEP,   //!< Reset after exiting deep sleep mode
        BROWNOUT  = ESP_RST_BROWNOUT,    //!< Brownout reset (software or hardware)
        SDIO      = ESP_RST_SDIO,        //!< Reset over SDIO
    };

    void Notify() const noexcept
    {
        auto reset_reason = static_cast<RR>(esp_reset_reason());

        std::string description_of_reset_reason{};
        switch (reset_reason) {
        case RR::UNKNOWN: description_of_reset_reason = "UNKNOWN"; break;
        case RR::POWERON: description_of_reset_reason = "POWERON"; break;
        case RR::EXT: description_of_reset_reason = "EXT"; break;
        case RR::SW: description_of_reset_reason = "SW"; break;
        case RR::PANIC: description_of_reset_reason = "PANIC"; break;
        case RR::INT_WDT: description_of_reset_reason = "INT_WDT"; break;
        case RR::TASK_WDT: description_of_reset_reason = "TASK_WDT"; break;
        case RR::WDT: description_of_reset_reason = "WDT"; break;
        case RR::DEEPSLEEP: description_of_reset_reason = "DEEPSLEEP"; break;
        case RR::BROWNOUT: description_of_reset_reason = "BROWNOUT"; break;
        case RR::SDIO: description_of_reset_reason = "SDIO"; break;
        }

        console.Log(description_of_reset_reason);
    }

  private:
    Logger console{ "ResetReason", ProjCfg::EnableLogForComponent::ResetReason };
};