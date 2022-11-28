#pragma once

#include <memory>
#include <map>
#include <functional>
#include <utility>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#include "esp_logger.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "project_configs.hpp"

#include "bluetooth_gap.hpp"

class Bluetooth;

// todo: to be removed:
#include "spp_task.h"

#ifndef SPP_TAG
#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#endif

class BluetoothSPP {
  public:
    using EventCallbackT = std::function<void()>;
    enum class Event {
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
    enum class DataManagementMode {
        ByCallback = 0,
        ByVFS
    };
    enum class SecurityMode {
        None         = 0x0000,
        Authorize    = 0x0001,
        Authenticate = 0x0012,
        Encrypt      = 0x0024,
        Mode4Level4  = 0x0040,
        MITM         = 0x3000,
        In16Digits   = 0x4000
    };
    enum class Role {
        Master = 0,
        Slave
    };
    std::shared_ptr<BluetoothSPP> static Create(DataManagementMode mode,
                                                SecurityMode       security,
                                                Role               new_role,
                                                std::string        device_name,
                                                std::string        server_name)
    {
        if (_this)
            std::terminate();

        _this = std::shared_ptr<BluetoothSPP>{
            new BluetoothSPP{ mode, security, new_role, std::move(device_name), std::move(server_name) }
        };

        return Get();
    }
    std::shared_ptr<BluetoothSPP> static Get() noexcept
    {
        if (_this)
            return _this;
        else
            std::terminate();
    }

    BluetoothSPP(BluetoothSPP const &other)         = delete;
    BluetoothSPP &operator=(BluetoothSPP const &rh) = delete;

    void SetEventCallback(Event for_event, EventCallbackT &&callback) noexcept
    {
        eventCallbacks[for_event] = std::move(callback);
    }

    // todo: make protected
    [[nodiscard]] auto EnumToEspNativeType(DataManagementMode mode) noexcept { return static_cast<esp_spp_mode_t>(mode); }
    [[nodiscard]] auto EnumToEspNativeType(Event event) noexcept { return static_cast<esp_spp_cb_event_t>(event); }
    [[nodiscard]] auto EnumToEspNativeType(SecurityMode security) noexcept
    {
        return static_cast<esp_spp_sec_t>(security);
    }
    [[nodiscard]] auto EnumToEspNativeType(Role role) noexcept { return static_cast<esp_spp_role_t>(role); }

    bool RegisterSPPCallback() noexcept
    {
        if (esp_spp_register_callback(
              [](esp_spp_cb_event_t event, esp_spp_cb_param_t *param) { MainCallback(event, param); }) != ESP_OK) {
            logger->LogError("spp callback registration failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool Init() noexcept
    {
        if (esp_spp_init(EnumToEspNativeType(dataManagementMode)) != ESP_OK) {
            logger->LogError("SPP Init failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    void LogOwnAddress() const noexcept
    {
        std::array<char, 18> bda_str;
        logger->Log("Own Address is: " +
                    std::string{ bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str.data(), bda_str.size()) });
    }
    void StartTasks() noexcept { sppTask.Start(); }

  protected:
    struct SppTaskMsg {
        uint16_t           signal;
        esp_spp_cb_event_t event;
        esp_spp_cb_param_t params;
    };

    //************************** HELPERS **************************//
    [[nodiscard]] char static *bda2str(uint8_t *bda, char *str, size_t size)
    {
        if (bda == NULL || str == NULL || size < 18) {
            return NULL;
        }

        uint8_t *p = bda;
        sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
        return str;
    }
    void InitializationFailedCallback() const noexcept { std::terminate(); }
    void InvokeCallbackForEvent(Event event) noexcept
    {
        if (eventCallbacks.find(event) != eventCallbacks.end()) {
            eventCallbacks.at(event)();
        }
    }
    void static MainCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
    {
        /* To avoid stucking Bluetooth stack, we dispatch the SPP callback event to the other lower priority task */
        //        spp_task_work_dispatch(SPPCallbackImpl, event, param, sizeof(esp_spp_cb_param_t), NULL);

        auto spp_msg = SppTaskMsg{ SPP_TASK_SIG_WORK_DISPATCH, event, static_cast<esp_spp_cb_param_t>(*param) };

        if (not _this->sppQueue.Send(spp_msg, ProjCfg::SppSendTimeoutMs))
            _this->logger->LogError("spp send timeouted!");
    }

    //************************VVV SPP Related methods VVV****************//
    void RegisterVFS() const noexcept { esp_spp_vfs_register(); }
    void StartServer() noexcept
    {
        if (sppServerStarted)
            return;

        esp_spp_start_srv(EnumToEspNativeType(securityMode), EnumToEspNativeType(role), 0, serverName.c_str());

        sppServerStarted = true;
    }

    [[noreturn]] void ReaderTask()
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

            InvokeCallbackForEvent(static_cast<Event>(spp_msg.event));

            switch (spp_msg.event) {
            case ESP_SPP_INIT_EVT:
                if (param.init.status == ESP_SPP_SUCCESS) {
                    logger->Log("Spp Initialize event");

                    RegisterVFS();
                    StartServer();
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
                    readerFileDescriptor = param.srv_open.fd;
                    sppReaderTask.Start();
                }
                break;
            default: logger->Log("Unhandled SPP Event:" + std::to_string(spp_msg.event));
            }
        }
    }

  private:
    BluetoothSPP(DataManagementMode mode,
                 SecurityMode       security,
                 Role               new_role,
                 std::string        device_name,
                 std::string        server_name)
      : dataManagementMode{ mode }
      , securityMode{ security }
      , role{ new_role }
      , logger{ EspLogger::Get() }
      , sppQueue{ ProjCfg::SppQueueLen, "spp queue" }
      , sppReaderTask{ [this]() { ReaderTask(); },
                       ProjCfg::SppReaderTaskStackSize,
                       ProjCfg::SppReaderTaskPrio,
                       "spp reader",
                       true }
      , sppTask{ [this]() { SPPTask(); }, ProjCfg::SppTaskStackSize, ProjCfg::SppTaskPrio, "bluetooth spp", true }
    { }

    std::shared_ptr<BluetoothSPP> static _this;

    DataManagementMode dataManagementMode;
    SecurityMode       securityMode;
    Role               role;
    std::string        deviceName;
    std::string        serverName;

    std::shared_ptr<EspLogger> logger;
    Queue<SppTaskMsg>          sppQueue;
    Task                       sppReaderTask;
    Task                       sppTask;

    //    std::shared_ptr<BluetoothGAP> gapDriver;

    int  readerFileDescriptor{ -1 };
    bool sppServerStarted{ false };

    std::map<Event, EventCallbackT> eventCallbacks;

    auto static constexpr SPP_DATA_LEN = 100;
};