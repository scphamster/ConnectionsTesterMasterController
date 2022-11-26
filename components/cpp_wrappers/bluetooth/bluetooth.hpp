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

    using LoggerT = EspLogger;

    Bluetooth(Bluetooth const &other)            = delete;
    Bluetooth &operator=(Bluetooth const &other) = delete;

    void static Create(BasisMode mode)
    {
        if (_this)
            return;

        _this = std::shared_ptr<Bluetooth>{ new Bluetooth{ mode } };
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
        using SppCallbackT = std::function<void(uint16_t, void *)>;

        uint16_t           signal;
        esp_spp_cb_event_t event;

        std::unique_ptr<esp_spp_cb_param_t> params;
        //        esp_spp_cb_param_t                  parameters;
        //        void                               *parameter;
    };

    //*************************VVVV Initializers VVVV*********************//
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
        //         Set default parameters for Secure Simple Pairing
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
        // todo: refactor

        switch (event) {
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(SPP_TAG, "authentication success: %s", param->auth_cmpl.device_name);
                esp_log_buffer_hex(SPP_TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            }
            else {
                ESP_LOGE(SPP_TAG, "authentication failed, status:%d", param->auth_cmpl.stat);
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
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_CFM_REQ_EVT Please compare the numeric value: %d", param->cfm_req.num_val);
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case ESP_BT_GAP_KEY_NOTIF_EVT:
            ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_NOTIF_EVT passkey:%d", param->key_notif.passkey);
            break;

        case ESP_BT_GAP_KEY_REQ_EVT: ESP_LOGI(SPP_TAG, "ESP_BT_GAP_KEY_REQ_EVT Please enter passkey!"); break;
#endif
        case ESP_BT_GAP_MODE_CHG_EVT: ESP_LOGI(SPP_TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode); break;
        default: ESP_LOGI(SPP_TAG, "event: %d", event); break;
        }
    }
    void SppTask() noexcept
    {
        while (true) {
            auto spp_msg = sppQueue.Receive();

            SPPCallbackImpl(spp_msg.event, spp_msg.params.get());   // fixme: do not use .get(), pass unique_ptr itself
        }
    }
    void static SPPCallbackImpl(uint16_t e, void *p) noexcept
    {
        esp_spp_cb_event_t  event       = static_cast<esp_spp_cb_event_t>(e);
        esp_spp_cb_param_t *param       = static_cast<esp_spp_cb_param_t *>(p);
        char                bda_str[18] = { 0 };

        switch (event) {
        case ESP_SPP_INIT_EVT:
            if (param->init.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(SPP_TAG, "ESP_SPP_INIT_EVT");
                /* Enable SPP VFS mode */
                esp_spp_vfs_register();
                esp_spp_start_srv(sec_mask, role_slave, 0, SPP_SERVER_NAME);
            }
            else {
                ESP_LOGE(SPP_TAG, "ESP_SPP_INIT_EVT status:%d", param->init.status);
            }
            break;
        case ESP_SPP_DISCOVERY_COMP_EVT: ESP_LOGI(SPP_TAG, "ESP_SPP_DISCOVERY_COMP_EVT"); break;
        case ESP_SPP_OPEN_EVT: ESP_LOGI(SPP_TAG, "ESP_SPP_OPEN_EVT"); break;
        case ESP_SPP_CLOSE_EVT:
            ESP_LOGI(SPP_TAG,
                     "ESP_SPP_CLOSE_EVT status:%d handle:%d close_by_remote:%d",
                     param->close.status,
                     param->close.handle,
                     param->close.async);
            break;
        case ESP_SPP_START_EVT:
            if (param->start.status == ESP_SPP_SUCCESS) {
                ESP_LOGI(SPP_TAG,
                         "ESP_SPP_START_EVT handle:%d sec_id:%d scn:%d",
                         param->start.handle,
                         param->start.sec_id,
                         param->start.scn);
                esp_bt_dev_set_device_name(EXAMPLE_DEVICE_NAME);
                esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            }
            else {
                ESP_LOGE(SPP_TAG, "ESP_SPP_START_EVT status:%d", param->start.status);
            }
            break;
        case ESP_SPP_CL_INIT_EVT: ESP_LOGI(SPP_TAG, "ESP_SPP_CL_INIT_EVT"); break;
        case ESP_SPP_SRV_OPEN_EVT:
            ESP_LOGI(SPP_TAG,
                     "ESP_SPP_SRV_OPEN_EVT status:%d handle:%d, rem_bda:[%s]",
                     param->srv_open.status,
                     param->srv_open.handle,
                     bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
            if (param->srv_open.status == ESP_SPP_SUCCESS) {
                spp_wr_task_start_up(ReadHandle, param->srv_open.fd);
            }
            break;
        default: break;
        }
    }
    void static SPPCallbackWrapper(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
    {
        /* To avoid stucking Bluetooth stack, we dispatch the SPP callback event to the other lower priority task */
        //        spp_task_work_dispatch(SPPCallbackImpl, event, param, sizeof(esp_spp_cb_param_t), NULL);

        auto spp_msg = SppTaskMsg{ SPP_TASK_SIG_WORK_DISPATCH,
                                   event,
                                   std::make_unique<esp_spp_cb_param_t>(static_cast<esp_spp_cb_param_t>(*param)) };

        if (not _this->sppQueue.Send(spp_msg, ProjCfg::SppSendTimeoutMs))
            _this->logger->LogError("spp send timeouted!");

        //        spp_task_msg_t msg;
        //        memset(&msg, 0, sizeof(spp_task_msg_t));

        //        msg.sig   = SPP_TASK_SIG_WORK_DISPATCH;
        //        msg.event = event;
        //        msg.cb    = p_cback;
        //
        //        if (param_len == 0) {
        //            return spp_task_send_msg(&msg);
        //        }
        //        else if (p_params && param_len > 0) {
        //            if ((msg.param = malloc(param_len)) != NULL) {
        //                memcpy(msg.param, p_params, param_len);
        //                /* check if caller has provided a copy callback to do the deep copy */
        //                if (p_copy_cback) {
        //                    p_copy_cback(&msg, msg.param, p_params);
        //                }
        //                return spp_task_send_msg(&msg);
        //            }
        //        }
        //
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
    void static ReadHandle(void *param)
    {
        int      size     = 0;
        int      fd       = (int)param;
        uint8_t *spp_data = NULL;

        spp_data = static_cast<uint8_t *>(malloc(SPP_DATA_LEN));
        if (!spp_data) {
            ESP_LOGE(SPP_TAG, "malloc spp_data failed, fd:%d", fd);
            goto done;
        }

        do {
            /* The frequency of calling this function also limits the speed at which the peer device can send data. */
            size = read(fd, spp_data, SPP_DATA_LEN);
            if (size < 0) {
                break;
            }
            else if (size == 0) {
                /* There is no data, retry after 500 ms */
                vTaskDelay(500 / portTICK_PERIOD_MS);
            }
            else {
                ESP_LOGI(SPP_TAG, "fd = %d data_len = %d", fd, size);
                esp_log_buffer_hex(SPP_TAG, spp_data, size);
                /* To avoid task watchdog */
                vTaskDelay(10 / portTICK_PERIOD_MS);
            }
        } while (1);
done:
        if (spp_data) {
            free(spp_data);
        }
        spp_wr_task_shut_down();
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
    //********************VVV Tasks VVV****************//
    [[noreturn]] void SPPTask() noexcept
    {
        while (true) { }
    }

  private:
    Bluetooth(BasisMode mode) noexcept
      : basisMode{ mode }
      , sppQueue{ ProjCfg::SppQueueLen, "spp queue" }
      , sppTask{ [this]() { SPPTask(); }, ProjCfg::SppTaskStackSize, ProjCfg::SppTaskPrio, "bluetooth spp" }
    {
        logger = EspLogger::Get();

        InitMemory();
        InitController();
        EnableController();
        BluedroidInit();
        BluedroidEnable();

        RegisterGAPCallback();
        RegisterSPPCallback();

        // VVVV implemented in initialization of task and queue members
        spp_task_task_start_up();

        SPPInit();
        SetPin();
        LogOwnAddress();
        //        std::array<char, 18> bda_str;
        //
        //        ESP_LOGI(SPP_TAG,
        //                 "Own address:[%s]",
        //                 bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str.data(), bda_str.size()));
    }

    std::shared_ptr<Bluetooth> static _this;
    std::shared_ptr<LoggerT> logger;
    BasisMode                basisMode;

    Queue<SppTaskMsg> sppQueue;
    Task              sppTask;

    esp_spp_mode_t static constexpr esp_spp_mode = ESP_SPP_MODE_VFS;   // todo: wtf is this
    esp_spp_sec_t static constexpr sec_mask      = ESP_SPP_SEC_AUTHENTICATE;
    esp_spp_role_t static constexpr role_slave   = ESP_SPP_ROLE_SLAVE;

    auto static constexpr SPP_DATA_LEN        = 100;
    auto static constexpr SPP_SERVER_NAME     = "SPP_SERVER";
    auto static constexpr EXAMPLE_DEVICE_NAME = "ESP_SPP_ACCEPTOR";
};