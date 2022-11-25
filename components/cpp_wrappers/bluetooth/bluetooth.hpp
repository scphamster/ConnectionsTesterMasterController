#pragma once
#include <memory>

#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
// #include "spp_task.h"

#include "esp_logger.hpp"

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
    void InitializationFailedCallback() const noexcept { std::terminate(); }
    bool InitMemory() noexcept
    {
        auto result = ESP_OK;

        switch (basisMode) {
        case BasisMode::BLE: result = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT); break;
        case BasisMode::Classic: result = esp_bt_controller_mem_release(ESP_BT_MODE_BLE); break;
        default: break;
        }

        if (result == ESP_OK) {
            logger->Log("controller mem release failed");
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
    bool EnableController() noexcept
    {
        if (esp_bt_controller_enable(GetEspBtMode()) != ESP_OK) {
            logger->LogError("controller enable failed");
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
    bool BluedroidEnable() noexcept
    {
        if (esp_bluedroid_enable() != ESP_OK) {
            logger->LogError("bluedroid start failed");
            InitializationFailedCallback();
            return false;
        }

        return true;
    }

    [[nodiscard]] esp_bt_mode_t GetEspBtMode() const noexcept
    {
        switch (basisMode) {
        case BasisMode::BLE: return ESP_BT_MODE_BLE;
        case BasisMode::Classic: return ESP_BT_MODE_CLASSIC_BT;
        case BasisMode::BTDM_Dual: return ESP_BT_MODE_BTDM;

        default: return ESP_BT_MODE_CLASSIC_BT;
        }
    }

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
    void static SPPCallback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
    {
        /* To avoid stucking Bluetooth stack, we dispatch the SPP callback event to the other lower priority task */
        //        spp_task_work_dispatch(esp_spp_cb, event, param, sizeof(esp_spp_cb_param_t), NULL);
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
        if (esp_spp_register_callback(SPPCallback) != ESP_OK) {
            logger->LogError("spp callback registration failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }

  private:
    Bluetooth(BasisMode mode) noexcept
      : basisMode{ mode }
    {
        logger = EspLogger::Get();

        InitMemory();
        InitController();
        EnableController();
        BluedroidInit();
        BluedroidEnable();

        RegisterGAPCallback();
        RegisterSPPCallback();

        //
        //        spp_task_task_start_up();
        //
        //        if (esp_spp_init(esp_spp_mode) != ESP_OK) {
        //            ESP_LOGE(SPP_TAG, "%s spp init failed", __func__);
        //            return;
        //        }
        //
        // #if (CONFIG_BT_SSP_ENABLED == true)
        //        /* Set default parameters for Secure Simple Pairing */
        //        esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
        //        esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_IO;
        //        esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
        // #endif
        //
        //        /*
        //     * Set default parameters for Legacy Pairing
        //     * Use variable pin, input pin code when pairing
        //         */
        //        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
        //        esp_bt_pin_code_t pin_code;
        //        esp_bt_gap_set_pin(pin_type, 0, pin_code);
        //
        //        ESP_LOGI(SPP_TAG, "Own address:[%s]", bda2str((uint8_t *)esp_bt_dev_get_address(), bda_str,
        //        sizeof(bda_str)));
    }

    std::shared_ptr<Bluetooth> static _this;
    std::shared_ptr<LoggerT> logger;
    BasisMode                basisMode;
};