#include <stdio.h>

#include "nvs_flash.h"
#include "esp_logger.hpp"


#include "task.hpp"
#include "bluetooth.hpp"
#include "queue.hpp"

//todo remove from here after tests
#include "iic.hpp"

void
startBT()
{
    auto logger = EspLogger::Get();

    Bluetooth::Create(Bluetooth::BasisMode::Classic, "hamster bluetooth");
    logger->Log("Bluetooth creation completed");
    auto bl = Bluetooth::Get();

    logger->Log("bluetooth initialized");

    while (true) {
        Task::DelayMs(1000);
    }
}

struct SomeTestData {
    //    uint8_t val1;
    //    uint8_t val2;
    //    uint8_t val3;
    //    uint8_t val4;
    //    uint8_t val5;
    //    uint8_t val6;
    //    uint8_t val7;
    //    uint8_t val8;
    //    char  some_char;
    float some_float;
};

void i2c_test()
{
    auto logger = EspLogger::Get();

    std::vector<uint8_t> data_out;
    data_out.push_back(55);

    IIC::Create(IIC::Role::Master, 33, 32, 50000);
    auto drver = IIC::Get();

    while (true) {
        logger->Log("Writing data to slave");
        auto retval = drver->WriteAndRead<SomeTestData>(0x25, data_out, 1000);

        //        logger->Log("slave answer:" + std::to_string(retval.val1));
        //        logger->Log("slave answer:" + std::to_string(retval.val2));
        //        logger->Log("slave answer:" + std::to_string(retval.val3));
        //        logger->Log("slave answer:" + std::to_string(retval.val4));
        //        logger->Log("slave answer:" + std::to_string(retval.val5));
        //        logger->Log("slave answer:" + std::to_string(retval.val6));
        //        logger->Log("slave answer:" + std::to_string(retval.val7));
        //        logger->Log("slave answer:" + std::to_string(retval.val8));
        logger->Log("float = " + std::to_string(retval.some_float));

        Task::DelayMs(500);
    }

}

extern "C" void
app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    Bluetooth::Create(Bluetooth::BasisMode::Classic, "esp hamster");
    auto bluetooth_driver = Bluetooth::Get();

    i2c_test();
}
