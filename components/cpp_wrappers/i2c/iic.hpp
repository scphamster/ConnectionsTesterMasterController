#pragma once

#define CONFIG_I2C_MASTER_SCL 32
#define CONFIG_I2C_MASTER_SDA 33

#include <memory>
#include <type_traits>
#include <vector>
#include <optional>
#include <mutex>

#include "FreeRTOS/FreeRTOS.h"
#include "driver/i2c.h"
#include "esp_logger.hpp"
#include "my_mutex.hpp"

class IIC {
  public:
    using BufferT           = std::vector<uint8_t>;
    using PeripheralAddress = uint8_t;
    using Byte              = uint8_t;
    enum class Role {
        Slave = 0,
        Master
    };
    enum class OperationResult {
        OK                 = 0,
        Fail               = -1,
        ErrNoMemory        = 0x101,
        ErrInvalidArgs     = 0x102,
        ErrInvalidState    = 0x103,
        ErrInvalidSize     = 0x104,
        ErrNotFound        = 0x105,
        ErrNotSupported    = 0x106,
        ErrTimeout         = 0x107,
        ErrInvalidResponse = 0x108,
        ErrInvalidCrc      = 0x109,
        ErrInvalidVersion  = 0x10A,
        ErrInvalidMac      = 0x10B,
        ErrNotFinished     = 0x10C,
        ErrWifiBase        = 0x3000,
        ErrMeshBase        = 0x4000,
        ErrFlashBase       = 0x6000,
        ErrHwCryptoBase    = 0xc000,
        ErrMemprotBase     = 0xd000,
    };

    void static Create(Role role, int sda_pin_num, int scl_pin_num, size_t clock_freq_hz) noexcept
    {
        _this = std::shared_ptr<IIC>{ new IIC{ role, sda_pin_num, scl_pin_num, clock_freq_hz } };
    }

    std::shared_ptr<IIC> static Get() noexcept
    {
        configASSERT(_this.get() != nullptr);

        return _this;
    }

    void SetNewFrequency() { }

    OperationResult Write(PeripheralAddress address, BufferT const &data_to_be_sent, size_t timeout_ms) noexcept
    {
        i2c_mutex.lock();
        auto result = static_cast<OperationResult>(i2c_master_write_to_device(i2cModuleNum,
                                                                              address,
                                                                              data_to_be_sent.data(),
                                                                              data_to_be_sent.size(),
                                                                              timeout_ms / portTICK_RATE_MS));
        i2c_mutex.unlock();

        if (result != OperationResult::OK) {
            logger.LogError("i2c write to device:" + std::to_string(address) +
                            " unsuccessful, error: " + std::to_string(static_cast<int>(result)));
        }

        return static_cast<OperationResult>(result);
    }
    template<typename ReturnType>
    std::pair<OperationResult, std::optional<ReturnType>> Read(PeripheralAddress address, TickType_t timeout_ms) noexcept
    {
        auto read_buffer = std::array<Byte, sizeof(ReturnType)>();

        i2c_mutex.lock();
        auto result = static_cast<OperationResult>(i2c_master_read_from_device(i2cModuleNum,
                                                                               address,
                                                                               read_buffer.data(),
                                                                               read_buffer.size(),
                                                                               pdMS_TO_TICKS(timeout_ms)));
        i2c_mutex.unlock();

        if (result != OperationResult::OK) {
            logger.LogError("Read error from addr:" + std::to_string(address) + " : " +
                            std::to_string(static_cast<int>(result)));
            return { result, std::nullopt };
        }

        return { result, *reinterpret_cast<ReturnType *>(read_buffer.data()) };
    }
    std::optional<std::vector<Byte>> Read(PeripheralAddress address, size_t bytes_to_read, size_t timeout_ms) noexcept
    {
        std::vector<Byte> buffer;
        buffer.resize(bytes_to_read);

        i2c_mutex.lock();
        auto result =
          i2c_master_read_from_device(i2cModuleNum, address, buffer.data(), buffer.size(), timeout_ms / portTICK_RATE_MS);
        i2c_mutex.unlock();

        if (result != ESP_OK) {
            logger.LogError("i2c read from device unsuccessful, error: " + std::to_string(result));
            return std::nullopt;
        }

        return buffer;
    }

    bool CheckIfSlaveWithAddressIsOnLine(PeripheralAddress address) noexcept
    {
        Byte dummy{ 0 };
        i2c_mutex.lock();
        auto result = i2c_master_write_to_device(i2cModuleNum, address, &dummy, 1, 0);
        i2c_mutex.unlock();

        return result == ESP_OK ? true : false;
    }

    template<typename ReturnType>
    std::optional<ReturnType> WriteAndRead(PeripheralAddress address,
                                           BufferT const    &data_to_be_sent,
                                           size_t            timeout_ms) noexcept
    {
        std::array<uint8_t, sizeof(ReturnType)> read_buffer{};

        i2c_mutex.lock();
        OperationResult res = i2c_master_write_read_device(i2cModuleNum,
                                                           address,
                                                           data_to_be_sent.data(),
                                                           data_to_be_sent.size(),
                                                           read_buffer.data(),
                                                           read_buffer.size(),
                                                           timeout_ms / portTICK_RATE_MS);
        i2c_mutex.unlock();

        if (res != OperationResult::OK) {
            logger.LogError("i2c write read not successful, error:" + std::to_string(static_cast<int>(res)));
            return { false, ReturnType{} };
        }

        return { true, *(reinterpret_cast<ReturnType *>(read_buffer.data())) };
    }

  protected:
  private:
    IIC(Role role, int sda_pin_num, int scl_pin_num, size_t clock_freq_hz) noexcept
      : logger{ "IIC", ProjCfg::EnableLogForComponent::IIC }
    {
        i2c_config_t conf{};

        conf.mode       = static_cast<i2c_mode_t>(role);
        conf.sda_io_num = sda_pin_num;
        conf.scl_io_num = scl_pin_num;
        // todo: make configurable
        conf.sda_pullup_en    = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en    = GPIO_PULLUP_ENABLE;
        conf.master.clk_speed = clock_freq_hz;
        i2c_param_config(i2cModuleNum, &conf);

        auto constexpr ignored_buffer_size_value = 0;

        auto res = i2c_driver_install(i2cModuleNum, conf.mode, ignored_buffer_size_value, ignored_buffer_size_value, 0);

        if (res != ESP_OK) {
            logger.LogError("failure upon I2C driver installation, Error=" + std::to_string(res));
        }
    }

    std::shared_ptr<IIC> static _this;
    SmartLogger logger;
    // todo: make configurable
    auto static constexpr i2cModuleNum                  = 0;
    TickType_t static constexpr slaveOnLineCheckTimeout = 0;
    Mutex i2c_mutex;
};
