#pragma once

#define CONFIG_I2C_MASTER_SCL 32
#define CONFIG_I2C_MASTER_SDA 33

#include <memory>
#include <type_traits>
#include <vector>

#include "FreeRTOS/FreeRTOS.h"
#include "driver/i2c.h"

#include "esp_logger.hpp"

class IIC {
  public:
    using BufferT           = std::vector<uint8_t>;
    using PeripheralAddress = uint8_t;
    enum class Role {
        Slave = 0,
        Master
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

    template<typename ReturnType>
    ReturnType WriteAndRead(PeripheralAddress address, BufferT const &data_to_be_sent, size_t timeout_ms) noexcept
    {
        std::array<uint8_t, sizeof(ReturnType)> read_buffer{};

        auto res = i2c_master_write_read_device(i2cModuleNum,
                                                address,
                                                data_to_be_sent.data(),
                                                data_to_be_sent.size(),
                                                read_buffer.data(),
                                                read_buffer.size(),
                                                timeout_ms / portTICK_RATE_MS);

        if (res != ESP_OK) {
            logger->LogError("i2c write read not successful, error:" + std::to_string(res));
        }

        return *(reinterpret_cast<ReturnType *>(read_buffer.data()));
    }

  protected:
  private:
    IIC(Role role, int sda_pin_num, int scl_pin_num, size_t clock_freq_hz)
    noexcept
      : logger{ EspLogger::Get() }
    {
        i2c_config_t conf{};

        conf.mode       = static_cast<i2c_mode_t>(role);
        conf.sda_io_num = sda_pin_num;
        conf.scl_io_num = scl_pin_num;

        // todo: make configurable
        conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
        conf.scl_pullup_en = GPIO_PULLUP_ENABLE;

        conf.master.clk_speed = clock_freq_hz;

        // todo: make configurable
        auto constexpr i2c_master_port = 0;
        i2c_param_config(i2c_master_port, &conf);

        auto constexpr ignored_buffer_size_value = 0;

        auto res =
          i2c_driver_install(i2c_master_port, conf.mode, ignored_buffer_size_value, ignored_buffer_size_value, 0);

        if (res != ESP_OK) {
            logger->LogError("failure upon I2C driver installation, Error=" + std::to_string(res));
        }
    }

    std::shared_ptr<IIC> static _this;
    std::shared_ptr<EspLogger> logger;
    auto static constexpr i2cModuleNum = 0;
};
