#include <stdio.h>

#include "nvs_flash.h"
#include "esp_logger.hpp"
#include "task.hpp"
#include "bluetooth.hpp"
#include "queue.hpp"

void
testtask()
{
    auto logger = EspLogger::Get();
    logger->Log("Testtask entereed");

    while (true) {
        logger->Log("Test");
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

    auto logger = EspLogger::Get();

    Bluetooth::Create(Bluetooth::BasisMode::Classic, "hamster bluetooth");
    logger->Log("Bluetooth creation completed");
    auto bl = Bluetooth::Get();

    logger->Log("bluetooth initialized");

    while (true) {
        Task::DelayMs(1000);
    }
}
