#pragma once
#include <memory>

#include "esp_log.h"
//#include "esp_bt.h"
//#include "esp_bt_main.h"
//#include "esp_gap_bt_api.h"
//#include "esp_bt_device.h"
//#include "esp_spp_api.h"
//#include "spp_task.h"

class Bluetooth {
  public:
    Bluetooth(Bluetooth const &other) = delete;
    Bluetooth &operator=(Bluetooth const &other) = delete;

    void static Create()
    {
        if (_this)
            return;

        _this = std::shared_ptr<Bluetooth>{ new Bluetooth{ 5 } };
    }

    std::shared_ptr<Bluetooth> static Get()
    {
        if (_this)
            return _this;

        else
            std::terminate();
    }

  protected:
  private:
    Bluetooth(int i) noexcept {



    }

    std::shared_ptr<Bluetooth> static _this;
};