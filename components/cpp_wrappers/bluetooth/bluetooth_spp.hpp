#pragma once

#include <memory>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#include "esp_logger.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "project_configs.hpp"

class BluetoothSPP {
  public:
    enum class SppEvent {
        Initialized             = 0,
        Uninitialized           = 1,
        DiscoveryCompleted      = 8,
        Open                    = 26,
        Close                   = 27,
        Start                   = 28,
        ClientInitiated         = 29,
        DataArrived             = 30,
        CongestionStatusChanged = 31,
        WriteCompleted          = 33,
        ServerConnectionOpen    = 34,
        ServerStop              = 35,
    };

    bool static RegisterSPPCallback() noexcept
    {
        auto logger = EspLogger::Get();

        if (esp_spp_register_callback(
              [](esp_spp_cb_event_t event, esp_spp_cb_param_t *param) { SPPCallback(event, param); }) != ESP_OK) {
            logger->LogError("spp callback registration failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }

  protected:
    struct SppTaskMsg {
        uint16_t           signal;
        esp_spp_cb_event_t event;
        esp_spp_cb_param_t params;
    };

    bool SPPInit() noexcept
    {
        if (esp_spp_init(esp_spp_mode) != ESP_OK) {
            logger->LogError("SPP Init failed");

            InitializationFailedCallback();
            return false;
        }

#if (CONFIG_BT_SSP_ENABLED == true)
        gapDriver.ConfigureSecurity();
#endif
        return true;
    }

    void InitializationFailedCallback() const noexcept { std::terminate(); }

    void static SPPCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
    {
        /* To avoid stucking Bluetooth stack, we dispatch the SPP callback event to the other lower priority task */
        //        spp_task_work_dispatch(SPPCallbackImpl, event, param, sizeof(esp_spp_cb_param_t), NULL);

        auto spp_msg = SppTaskMsg{ SPP_TASK_SIG_WORK_DISPATCH, event, static_cast<esp_spp_cb_param_t>(*param) };

        if (not _this->sppQueue.Send(spp_msg, ProjCfg::SppSendTimeoutMs))
            _this->logger->LogError("spp send timeouted!");
    }


    void ReadHandle()
    {
        int  size = 0;
        auto fd   = readerFileDescriptor;

        auto data = std::vector<char>{};
        data.resize(SPP_DATA_LEN);

        do {
            /* The frequency of calling this function also limits the speed at which the peer device can send data. */
            //            logger->Log("Reading file descriptor");

            size = read(fd, data.data(), data.size());
            if (size < 0) {
                logger->Log("SPP File descriptor read failed");
                std::terminate();
                break;
            }
            else if (size == 0) {
                /* There is no data, retry after 500 ms */
                Task::DelayMs(500);
            }
            else {
                logger->Log("Data Arrived! Bytes Num:" + std::to_string(size) + " Data:" + data.data());

                /* To avoid task watchdog */
                Task::DelayMs(10);
            }
        } while (true);
    }

    [[noreturn]] void SPPTask() noexcept
    {
        std::array<char, 18> bda_str{};

        while (true) {
            auto spp_msg = sppQueue.Receive();

            auto           param = spp_msg.params;
            auto           str   = std::string{ "DUPA" };
            const uint8_t *u8str = reinterpret_cast<const uint8_t *>(str.c_str());
            switch (spp_msg.event) {
            case ESP_SPP_INIT_EVT:
                if (param.init.status == ESP_SPP_SUCCESS) {
                    logger->Log("Spp Initialize event");

                    SPPRegisterVFS();
                    SPPStartServer();
                }
                else {
                    logger->LogError("SPP Init failed, error code:" + std::to_string(spp_msg.params.init.status));
                }
                break;
            case ESP_SPP_DISCOVERY_COMP_EVT: ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT"); break;
            case ESP_SPP_OPEN_EVT:
                logger->Log("SPP Open Event! Writing Some Data...");

                esp_spp_write(param.open.handle, 5, const_cast<uint8_t *>(u8str));
                break;
            case ESP_SPP_CLOSE_EVT:
                ESP_LOGI(SPP_TAG,
                         "ESP_SPP_CLOSE_EVT status:%d handle:%d close_by_remote:%d",
                         param.close.status,
                         param.close.handle,
                         param.close.async);
                break;
            case ESP_SPP_START_EVT:
                if (param.start.status == ESP_SPP_SUCCESS) {
                    logger->Log("SPP start event, handle=" + std::to_string(param.start.handle) +
                                " security id:" + std::to_string(param.start.sec_id) +
                                " server channel:" + std::to_string(param.start.scn));

                    SetDeviceName();
                    gapDriver.SetGAPScanMode(ConnectabilityMode::Connectable, DiscoverabilityMode::General);
                }
                else {
                    logger->LogError("SPP start event failed, status:" + std::to_string(param.start.status));
                }
                break;
            case ESP_SPP_CL_INIT_EVT: logger->Log("Client initiated connection!"); break;
            case ESP_SPP_SRV_OPEN_EVT:
                logger->Log("Server open event! Status:" + std::to_string(param.srv_open.status) +
                            " Handle:" + std::to_string(param.srv_open.handle) +
                            " rem bda:" + bda2str(param.srv_open.rem_bda, bda_str.data(), sizeof(bda_str)));

                if (param.srv_open.status == ESP_SPP_SUCCESS) {
                    //                    spp_wr_task_start_up(ReadHandle, param.srv_open.fd);
                    readerFileDescriptor = param.srv_open.fd;
                    sppReaderTask.Start();
                }
                break;
            default: logger->Log("Unhandled SPP Event:" + std::to_string(spp_msg.event));
            }
        }
    }

  private:
    BluetoothSPP()
      : logger{ EspLogger::Get() }
      , sppQueue{ ProjCfg::SppQueueLen, "spp queue" }
      , sppReaderTask{ [this]() { ReadHandle(); },
                       ProjCfg::SppReaderTaskStackSize,
                       ProjCfg::SppReaderTaskPrio,
                       "spp reader",
                       true }
      , sppTask{ [this]() { SPPTask(); }, ProjCfg::SppTaskStackSize, ProjCfg::SppTaskPrio, "bluetooth spp", true }
    { }

    std::shared_ptr<BluetoothSPP> static _this;
    std::shared_ptr<EspLogger> logger;
    Queue<SppTaskMsg>          sppQueue;
    Task                       sppReaderTask;
    Task                       sppTask;

    int readerFileDescriptor{ -1 };

};