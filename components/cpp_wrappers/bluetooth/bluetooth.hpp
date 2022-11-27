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
    enum class ConnectabilityMode {
        Connectable,
        NonConnectable
    };
    enum class DiscoverabilityMode {
        Hidden,
        Limited,
        General
    };

    using LoggerT     = EspLogger;
    using DeviceNameT = std::string;

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
    struct SppTaskMsg {
        uint16_t           signal;
        esp_spp_cb_event_t event;
        esp_spp_cb_param_t params;
    };

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

        RegisterGAPCallback();
        RegisterSPPCallback();
        logger->Log("Callbacks registered, initializing spp...");

        // VVVV implemented in initialization of task and queue members
        //        spp_task_task_start_up();

        SPPInit();
        logger->Log("Spp initialized, setting pin...");

        SetPin();
        logger->Log("Pin set, bluetooth configurations completed");

        LogOwnAddress();
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
    bool RegisterGAPCallback() noexcept
    {
        if (esp_bt_gap_register_callback(GAPCallback) != ESP_OK) {
            logger->LogError("GAP callback register failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool RegisterSPPCallback() noexcept
    {
        if (esp_spp_register_callback(
              [](esp_spp_cb_event_t event, esp_spp_cb_param_t *param) { SPPCallbackWrapper(event, param); }) != ESP_OK) {
            logger->LogError("spp callback registration failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }
    bool SPPInit() noexcept
    {
        if (esp_spp_init(esp_spp_mode) != ESP_OK) {
            logger->LogError("SPP Init failed");

            InitializationFailedCallback();
            return false;
        }

#if (CONFIG_BT_SSP_ENABLED == true)
        //Set default parameters for Secure Simple Pairing
        esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
        esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_IO;
        esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
#endif

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

    //**************************VVV Bluetooth related callbacks VVV*******************//
    void static GAPCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
    {
        auto logger = _this->logger;

        switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT:
            logger->Log("Discovery event! device name: _*NOT IMPLEMENTED*_");
//
//              for (int i = 0; i < param->disc_res.num_prop; i++){
//                if (param->disc_res.prop[i].type == ESP_BT_GAP_DEV_PROP_EIR
//                    && get_name_from_eir(param->disc_res.prop[i].val, peer_bdname, &peer_bdname_len)){
//                    ESP_LOGI(SPP_TAG, "Found peer: %s", peer_bdname);
//                    //                esp_log_buffer_char(SPP_TAG, peer_bdname, peer_bdname_len);
//                    if (strlen(remote_device_name) == peer_bdname_len
//                        && strncmp(peer_bdname, remote_device_name, peer_bdname_len) == 0) {
//                        memcpy(peer_bd_addr, param->disc_res.bda, ESP_BD_ADDR_LEN);
//                        ESP_LOGI(SPP_TAG, "Found searched device...");
//
//                        /* Have found the target peer device, cancel the previous GAP discover procedure. And go on
//                     * dsicovering the SPP service on the peer device */
//                        esp_bt_gap_cancel_discovery();
//                        esp_spp_start_discovery(peer_bd_addr);
//                    }
//                }
//            }

          break;

        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                logger->Log("Authentication success, paired with device named:" +
                            std::string{ reinterpret_cast<char *>(param->auth_cmpl.device_name) });
            }
            else {
                logger->LogError("Authentication failed, status:" + std::to_string(param->auth_cmpl.stat));
            }
            break;
        }
        case ESP_BT_GAP_PIN_REQ_EVT: {
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(SPP_TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = { 0 };
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            }
            else {
                ESP_LOGI(SPP_TAG, "Input pin code: 1234");
                esp_bt_pin_code_t pin_code;
                pin_code[0] = '1';
                pin_code[1] = '2';
                pin_code[2] = '3';
                pin_code[3] = '4';
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }

#if (CONFIG_BT_SSP_ENABLED == true)
        case ESP_BT_GAP_CFM_REQ_EVT:
            logger->Log("User confirmation request, please compare numeric value:" +
                        std::to_string(param->cfm_req.num_val));

            //            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d",
            //            param->cfm_req.num_val);
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
            break;

        case ESP_BT_GAP_KEY_REQ_EVT: ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!"); break;
#endif
        case ESP_BT_GAP_MODE_CHG_EVT: ESP_LOGI(SPP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode); break;
        default: logger->Log("Unhandled GAP Event:" + std::to_string(event));
        }
    }
    void static SPPCallbackWrapper(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
    {
        /* To avoid stucking Bluetooth stack, we dispatch the SPP callback event to the other lower priority task */
        //        spp_task_work_dispatch(SPPCallbackImpl, event, param, sizeof(esp_spp_cb_param_t), NULL);

        auto spp_msg = SppTaskMsg{ SPP_TASK_SIG_WORK_DISPATCH, event, static_cast<esp_spp_cb_param_t>(*param) };

        if (not _this->sppQueue.Send(spp_msg, ProjCfg::SppSendTimeoutMs))
            _this->logger->LogError("spp send timeouted!");
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
    [[nodiscard]] char static *bda2str(uint8_t *bda, char *str, size_t size)
    {
        if (bda == NULL || str == NULL || size < 18) {
            return NULL;
        }

        uint8_t *p = bda;
        sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
        return str;
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
    void SetPin() noexcept
    {
        // todo: make configurable

        //* Set default parameters for Legacy Pairing
        //* Use variable pin, input pin code when pairing
        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
        esp_bt_pin_code_t pin_code;
        esp_bt_gap_set_pin(pin_type, 0, pin_code);
    }
    void LogOwnAddress() const noexcept
    {
        std::array<char, 18> bda_str;
        logger->Log("Own Address is: " +
                    std::string{ bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str.data(), bda_str.size()) });
    }
    void SetDeviceName() noexcept { esp_bt_dev_set_device_name(deviceName.c_str()); }

    //************************VVV SPP Related methods VVV****************//
    void SPPRegisterVFS() const noexcept { esp_spp_vfs_register(); }
    void SPPStartServer() noexcept
    {
        if (sppServerStarted)
            return;

        // todo: make configurable
        esp_spp_start_srv(sec_mask, role_slave, 0, SPP_SERVER_NAME);

        sppServerStarted = true;
    }

    //***************************VVV GAP Related methods VVV****************//
    void SetGAPScanMode(ConnectabilityMode connectability, DiscoverabilityMode discoverability) noexcept
    {
        esp_bt_connection_mode_t connection_mode;
        esp_bt_discovery_mode_t  discovery_mode;

        switch (connectability) {
        case ConnectabilityMode::Connectable: connection_mode = ESP_BT_CONNECTABLE; break;
        case ConnectabilityMode::NonConnectable: connection_mode = ESP_BT_NON_CONNECTABLE; break;
        }

        switch (discoverability) {
        case DiscoverabilityMode::Hidden: discovery_mode = ESP_BT_NON_DISCOVERABLE; break;
        case DiscoverabilityMode::Limited: discovery_mode = ESP_BT_LIMITED_DISCOVERABLE; break;
        case DiscoverabilityMode::General: discovery_mode = ESP_BT_GENERAL_DISCOVERABLE; break;
        }

        esp_bt_gap_set_scan_mode(connection_mode, discovery_mode);
    }

    //********************VVV Tasks VVV****************//
    [[noreturn]] void SPPTask() noexcept
    {
        std::array<char, 18> bda_str{};

        while (true) {
            auto spp_msg = sppQueue.Receive();

            auto param = spp_msg.params;

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
            case ESP_SPP_OPEN_EVT: ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT"); break;
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
                    SetGAPScanMode(ConnectabilityMode::Connectable, DiscoverabilityMode::General);
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
    Bluetooth(BasisMode mode, DeviceNameT new_device_name) noexcept
      : logger{ EspLogger::Get() }
      , basisMode{ mode }
      , deviceName{ new_device_name }
      , sppQueue{ ProjCfg::SppQueueLen, "spp queue" }
      , sppTask{ [this]() { SPPTask(); }, ProjCfg::SppTaskStackSize, ProjCfg::SppTaskPrio, "bluetooth spp", true }
      , sppReaderTask{ [this]() { ReadHandle(); },
                       ProjCfg::SppReaderTaskStackSize,
                       ProjCfg::SppReaderTaskPrio,
                       "spp reader",
                       true }
    {
        sppTask.Start();
    }

    std::shared_ptr<Bluetooth> static _this;
    std::shared_ptr<LoggerT> logger;
    BasisMode                basisMode;
    DeviceNameT              deviceName;
    Queue<SppTaskMsg>        sppQueue;
    Task                     sppTask;
    Task                     sppReaderTask;
    bool                     sppServerStarted{ false };

    // todo: collect spp related data to single struct
    int readerFileDescriptor{ -1 };

    esp_spp_mode_t static constexpr esp_spp_mode = ESP_SPP_MODE_VFS;   // todo: wtf is this
    esp_spp_sec_t static constexpr sec_mask      = ESP_SPP_SEC_AUTHENTICATE;
    esp_spp_role_t static constexpr role_slave   = ESP_SPP_ROLE_SLAVE;

    auto static constexpr SPP_DATA_LEN        = 100;
    auto static constexpr SPP_SERVER_NAME     = "SPP_SERVER";
    auto static constexpr EXAMPLE_DEVICE_NAME = "ESP_SPP_ACCEPTOR";
};