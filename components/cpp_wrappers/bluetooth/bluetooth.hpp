#pragma once

#include <memory>
#include <array>
#include <functional>

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"

#include "esp_logger.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "project_configs.hpp"

// todo: headers to be deleted after refactoring:
#include "esp_log.h"

// todo: contents of headers below should be implemented here:
#include "spp_task.h"
#include "bluetooth_gap.hpp"
#include "bluetooth_spp.hpp"

#ifndef SPP_TAG
#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#endif

class Bluetooth {
  public:
    enum class BasisMode {
        BLE,
        Classic,
        BTDM_Dual
    };
    using ConnectabilityMode  = typename BluetoothGAP::ConnectabilityMode;
    using DiscoverabilityMode = typename BluetoothGAP::DiscoverabilityMode;
    using LoggerT             = EspLogger;
    using DeviceNameT         = std::string;

    Bluetooth(Bluetooth const &other)            = delete;
    Bluetooth &operator=(Bluetooth const &other) = delete;
    void static Create(BasisMode mode, DeviceNameT device_name)
    {
        if (_this)
            return;

        _this = std::shared_ptr<Bluetooth>{ new Bluetooth{ mode, device_name } };
        _this->Initialize();
    }
    std::shared_ptr<Bluetooth> static Get()
    {
        if (_this)
            return _this;

        else
            std::terminate();
    }

  protected:
    //*************************VVVV Initializers VVVV*********************//
    void Initialize() noexcept
    {
        logger->Log("Initializing bluetooth");

        InitMemory();
        InitController();
        EnableController();
        BluedroidInit();
        BluedroidEnable();
        logger->Log("Initialization complete, registering callbacks...");

        gapDriver->RegisterGAPCallback();
        sppDriver->RegisterSPPCallback();
        logger->Log("Callbacks registered, initializing spp...");

        // VVVV implemented in initialization of task and queue members
        //        spp_task_task_start_up();

        sppDriver->Init();
#if (CONFIG_BT_SSP_ENABLED == true)
        gapDriver->ConfigureSecurity();
#endif
        sppDriver->StartTasks();

        logger->Log("Spp initialized, setting pin...");

        gapDriver->SetPin();
        logger->Log("Pin set, bluetooth configurations completed");

        sppDriver->LogOwnAddress();
    }
    void InitializationFailedCallback() const noexcept { std::terminate(); }
    bool InitMemory() noexcept
    {
        auto result = ESP_OK;

        switch (basisMode) {
        case BasisMode::BLE: result = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); break;
        case BasisMode::Classic: result = esp_bt_controller_mem_release(ESP_BT_MODE_BLE); break;
        default: break;
        }

        if (result != ESP_OK) {
            logger->LogError("controller mem release failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool InitController() noexcept
    {
        // todo: make configurable
        esp_bt_controller_config_t configs = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        if (esp_bt_controller_init(&configs) != ESP_OK) {
            logger->LogError("bluetooth controller init failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool BluedroidInit() noexcept
    {
        if (esp_bluedroid_init() != ESP_OK) {
            logger->LogError("bluedroid init failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }

    //**************************VVV State Switchers VVV*****************//
    bool BluedroidEnable() noexcept
    {
        if (esp_bluedroid_enable() != ESP_OK) {
            logger->LogError("bluedroid start failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool EnableController() noexcept
    {
        if (esp_bt_controller_enable(GetEspBtMode()) != ESP_OK) {
            logger->LogError("controller enable failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }

    //*********************VVV Helpers VVV******************************//
    [[nodiscard]] esp_bt_mode_t GetEspBtMode() const noexcept
    {
        switch (basisMode) {
        case BasisMode::BLE: return ESP_BT_MODE_BLE;
        case BasisMode::Classic: return ESP_BT_MODE_CLASSIC_BT;
        case BasisMode::BTDM_Dual: return ESP_BT_MODE_BTDM;

        default: return ESP_BT_MODE_CLASSIC_BT;
        }
    }

    void SetDeviceName(std::string new_name) noexcept { esp_bt_dev_set_device_name(new_name.c_str()); }

  private:
    Bluetooth(BasisMode mode, DeviceNameT new_device_name) noexcept
      : logger{ EspLogger::Get() }
      , basisMode{ mode }
      , deviceName{ new_device_name }
      , gapDriver{ BluetoothGAP::Get() }
      , sppDriver{ BluetoothSPP::Create(BluetoothSPP::DataManagementMode::ByVFS,
                                        BluetoothSPP::SecurityMode::Authenticate,
                                        BluetoothSPP::Role::Slave,
                                        "hamster test",
                                        "hamster server") }
    {
        sppDriver->SetGapDriver(gapDriver);
        sppDriver->SetEventCallback(BluetoothSPP::Event::Start, [this]() {
            SetDeviceName(deviceName);
            gapDriver->SetGAPScanMode(BluetoothGAP::ConnectabilityMode::Connectable, BluetoothGAP::DiscoverabilityMode::General);
        });
    }

    std::shared_ptr<Bluetooth> static _this;
    std::shared_ptr<LoggerT> logger;
    BasisMode                basisMode;
    DeviceNameT              deviceName;

    std::shared_ptr<BluetoothGAP> gapDriver;
    std::shared_ptr<BluetoothSPP> sppDriver;

    // todo: cleanup:

    // todo: collect spp related data to single struct

    esp_spp_mode_t static constexpr esp_spp_mode = ESP_SPP_MODE_VFS;   // todo: wtf is this
    esp_spp_sec_t static constexpr sec_mask      = ESP_SPP_SEC_AUTHENTICATE;
    esp_spp_role_t static constexpr role_slave   = ESP_SPP_ROLE_SLAVE;

    auto static constexpr SPP_DATA_LEN        = 100;
    auto static constexpr SPP_SERVER_NAME     = "SPP_SERVER";
    auto static constexpr EXAMPLE_DEVICE_NAME = "ESP_SPP_ACCEPTOR";
};