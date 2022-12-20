#pragma once

#include <memory>
#include <map>
#include <functional>
#include <utility>
#include <mutex>

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
    using EventCallbackT    = std::function<void()>;
    using ConnectionHandleT = uint32_t;
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
    std::shared_ptr<BluetoothSPP> static Create(DataManagementMode           mode,
                                                SecurityMode                 security,
                                                Role                         new_role,
                                                std::string                  device_name,
                                                std::string                  server_name,
                                                std::shared_ptr<Queue<char>> input_queue)
    {
        if (_this)
            std::terminate();

        _this = std::shared_ptr<BluetoothSPP>{ new BluetoothSPP{ mode,
                                                                 security,
                                                                 new_role,
                                                                 std::move(device_name),
                                                                 std::move(server_name),
                                                                 std::move(input_queue) } };

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
    void SetStreamBufferToTransmitter(std::shared_ptr<StreamBuffer<char>> new_sb) noexcept
    {
        streamBufferToTransmitter = new_sb;
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
            logger.LogError("spp callback registration failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool Init() noexcept
    {
        if (esp_spp_init(EnumToEspNativeType(dataManagementMode)) != ESP_OK) {
            logger.LogError("SPP Init failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    void LogOwnAddress() noexcept
    {
        std::array<char, 18> bda_str;
        logger.Log("Own Address is: " +
                    std::string{ bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str.data(), bda_str.size()) });
    }
    void StartTasks() noexcept { sppTask.Start(); }
    template<size_t NumberOfBytes>
    void Write(std::array<char, NumberOfBytes> data) noexcept
    {
        std::lock_guard<Mutex>{ fileMutex };
        write(ioFileDescriptor, data.data(), data.size() * sizeof(data.at(0)));
    }
    void Write(std::string text) noexcept
    {
        std::lock_guard<Mutex>{ fileMutex };
        auto writen_bytes_number = write(ioFileDescriptor, text.data(), text.size());
        logger.Log("Data written to file, bytes written:" + std::to_string(writen_bytes_number));
    }

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
            _this->logger.LogError("spp send timeouted!");
    }
    void RegisterVFS() const noexcept { esp_spp_vfs_register(); }
    void StartServer() noexcept
    {
        if (serverStarted)
            return;

        esp_spp_start_srv(EnumToEspNativeType(securityMode), EnumToEspNativeType(role), 0, serverName.c_str());

        serverStarted = true;
    }

    //****************************** TASKS *************************//
    [[noreturn]] void WriterTask()
    {
        auto some_str = std::string{ "respond\n" };
        //        auto data_for_file = const_cast<uint8_t *>(reinterpret_cast<const uint8_t *>(some_str.c_str()));
        auto data_for_file = some_str.data();
        while (true) {
            {
                std::lock_guard<Mutex>{ fileMutex };
                auto writen_bytes_number = write(ioFileDescriptor, data_for_file, some_str.size());
                logger.Log("Data written to file, bytes written:" + std::to_string(writen_bytes_number));
            }

            Task::DelayMs(500);
        }
    }
    [[noreturn]] void ReaderTask()
    {
        auto data = std::vector<char>{};
        data.resize(SPP_DATA_LEN);
        int bytes_read{ 0 };

        do {
            {
                std::lock_guard<Mutex>{ fileMutex };
                bytes_read = read(ioFileDescriptor, data.data(), data.size());
            }

            if (bytes_read < 0) {
                logger.Log("SPP File descriptor read failed");
                std::terminate();
                break;
            }
            else if (bytes_read == 0) {
                /* There is no data, retry after 500 ms */
                Task::DelayMs(500);
            }
            else {
                logger.Log("SPP: New data arrived:" + std::string{ data.data() });
                for (int byte = 0; byte < bytes_read; byte++) {
                    inputQueue->SendImmediate(data.at(byte));
                }

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
            if (not spp_msg) {
                logger.LogError("Bluetooth spp queue reception failed");
                continue;
            }
            auto param = spp_msg->params;
            auto event = static_cast<Event>(spp_msg->event);

            InvokeCallbackForEvent(event);

            switch (event) {
            case Event::Initialized:
                if (param.init.status == ESP_SPP_SUCCESS) {
                    logger.Log("Spp Initialize event");

                    RegisterVFS();
                    StartServer();
                }
                else {
                    logger.LogError("SPP Init failed, error code:" + std::to_string(spp_msg->params.init.status));
                }
                break;
            case Event::DiscoveryCompleted: logger.Log("SPP: Discovery complete event!"); break;
            case Event::Open: logger.Log("SPP Open Event"); break;
            case Event::Close: logger.Log("SPP Close event"); break;
            case Event::Start:
                if (param.start.status == ESP_SPP_SUCCESS) {
                    logger.Log("SPP start event, handle=" + std::to_string(param.start.handle) +
                                " security id:" + std::to_string(param.start.sec_id) +
                                " server channel:" + std::to_string(param.start.scn));

                    connectionHandle = param.start.handle;
                }
                else {
                    logger.LogError("SPP start event failed, status:" + std::to_string(param.start.status));
                }
                break;
            case Event::ClientInitiated: logger.Log("Client initiated connection!"); break;
            case Event::ServerConnectionOpen:
                logger.Log("Server open event! Status:" + std::to_string(param.srv_open.status) +
                            " Handle:" + std::to_string(param.srv_open.handle) +
                            " Peer address:" + bda2str(param.srv_open.rem_bda, bda_str.data(), sizeof(bda_str)));

                if (param.srv_open.status == ESP_SPP_SUCCESS) {
                    ioFileDescriptor = param.srv_open.fd;
                    sppReaderTask.Start();
                    //                    writerTask.Start();
                }
                break;
            default: logger.Log("Unhandled SPP Event:" + std::to_string(spp_msg->event));
            }
        }
    }

  private:
    BluetoothSPP(DataManagementMode           mode,
                 SecurityMode                 security,
                 Role                         new_role,
                 std::string                  device_name,
                 std::string                  server_name,
                 std::shared_ptr<Queue<char>> input_queue)
      : dataManagementMode{ mode }
      , securityMode{ security }
      , role{ new_role }
      , logger{ "bluetooth spp:", ProjCfg::ComponentLogSwitch::BluetoothSPP }
      , sppQueue{ ProjCfg::SppQueueLen, "spp queue" }
      , sppReaderTask{ [this]() { ReaderTask(); },
                       ProjCfg::SppReaderTaskStackSize,
                       ProjCfg::SppReaderTaskPrio,
                       "spp reader",
                       true }
      , writerTask([this]() { WriterTask(); },
                   ProjCfg::SppWriterTaskStackSize,
                   ProjCfg::SppWriterTaskPrio,
                   "spp writer",
                   true)
      , sppTask{ [this]() { SPPTask(); }, ProjCfg::SppTaskStackSize, ProjCfg::SppTaskPrio, "bluetooth spp", true }
      , inputQueue{ input_queue }
    { }

    std::shared_ptr<BluetoothSPP> static _this;

    DataManagementMode dataManagementMode;
    SecurityMode       securityMode;
    Role               role;
    std::string        deviceName;
    std::string        serverName;

    SmartLogger                  logger;
    Queue<SppTaskMsg>            sppQueue;
    Task                         sppReaderTask;
    Task                         writerTask;
    Task                         sppTask;
    std::shared_ptr<Queue<char>> inputQueue;

    mutable Mutex fileMutex;

    ConnectionHandleT connectionHandle;

    std::shared_ptr<StreamBuffer<char>> streamBufferToTransmitter;

    int  ioFileDescriptor{ -1 };
    bool serverStarted{ false };

    std::map<Event, EventCallbackT> eventCallbacks;

    auto static constexpr SPP_DATA_LEN = 100;
};