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

#ifndef SPP_TAG
#define SPP_TAG "SPP_ACCEPTOR_DEMO"
#endif

class BluetoothGAP {
  public:
    enum class Event {
        DiscoveryCompleted = 0,
        DiscoveryStateChange,
        RemoteServices,
        RemoteServicesRecord,
        AuthenticationCompleted,
        PinRequest,
        SSPUserConfirmationRequest,
        SSPPasskeyNotification,
        SSPPasskeyRequest,
        ReadRSSI,
        EIRDataConfig,
        AFHChannelsSet,
        ReadRemoteName,
        ModeChange,
        BondedDeviceRemoved,
        QOSCompleted
    };
    enum class DeviceProperty {
        Name = 1,
        ClassOfDevice,
        RSSI,
        ExtendedInquiryResponse
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

    BluetoothGAP(BluetoothGAP const &other)          = delete;
    BluetoothGAP &operator=(BluetoothGAP const &rhs) = delete;

    std::shared_ptr<BluetoothGAP> static Get() noexcept
    {
        if (_this)
            return _this;

        return _this = std::shared_ptr<BluetoothGAP>{ new BluetoothGAP };
    }

    static auto EnumClassToEspNativeType(Event event) noexcept { return static_cast<esp_bt_gap_cb_event_t>(event); }
    static auto EnumClassToEspNativeType(DeviceProperty device_property) noexcept
    {
        return static_cast<esp_bt_gap_dev_prop_type_t>(device_property);
    }

    void static GAPCallback(esp_bt_gap_cb_event_t _event, esp_bt_gap_cb_param_t *param)
    {
        auto logger = EspLogger::Get();

        auto event = static_cast<Event>(_event);

        switch (event) {
        case Event::DiscoveryCompleted: logger->Log("Discovery event! device name: _*NOT IMPLEMENTED*_"); break;
        case Event::AuthenticationCompleted: {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                logger->Log("Authentication success, paired with device named:" +
                            std::string{ reinterpret_cast<char *>(param->auth_cmpl.device_name) });
            }
            else {
                logger->LogError("Authentication failed, status:" + std::to_string(param->auth_cmpl.stat));
            }
            break;
        }
        case Event::PinRequest: {
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
        case Event::SSPUserConfirmationRequest:
            logger->Log("GAP: User confirmation request, please compare numeric value:" +
                        std::to_string(param->cfm_req.num_val));
            esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
            break;

        case Event::SSPPasskeyNotification:
            logger->Log("GAP: Passkey notification event! Passkey:" + std::to_string(param->key_notif.passkey));
            break;

        case Event::SSPPasskeyRequest: logger->Log("GAP: Passkey request, please enter key"); break;
#endif
        case Event::ModeChange:
            logger->Log("GAP: Mode Change event! New mode:" + std::to_string(param->mode_chg.mode));
            break;

        default: logger->Log("GAP: Unhandled event:" + std::to_string(_event));
        }
    }
    void static ConfigureSecurity() noexcept
    {
        // Set default parameters for Secure Simple Pairing
        esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
        esp_bt_io_cap_t   iocap      = ESP_BT_IO_CAP_IO;
        esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));
    }
    void static SetPin() noexcept
    {
        // todo: make configurable

        //* Set default parameters for Legacy Pairing
        //* Use variable pin, input pin code when pairing
        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
        esp_bt_pin_code_t pin_code;
        esp_bt_gap_set_pin(pin_type, 0, pin_code);
    }
    void static SetGAPScanMode(ConnectabilityMode connectability, DiscoverabilityMode discoverability) noexcept
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

    bool RegisterGAPCallback() noexcept
    {
        if (esp_bt_gap_register_callback(GAPCallback) != ESP_OK) {
            EspLogger::Get()->LogError("GAP callback register failed");

            InitializationFailedCallback();
            return false;
        }

        return true;
    }

  protected:
    void InitializationFailedCallback() const noexcept { std::terminate(); }

  private:
    BluetoothGAP() = default;
    std::shared_ptr<BluetoothGAP> static _this;
};
