#include "nvs_flash.h"

#include "task.hpp"
#include "bluetooth.hpp"

#include "main_apparatus.hpp"

extern "C" void
app_main()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    auto bluetooth_to_apparatusQ = std::make_shared<Queue<char>>(100, "from bluetooth");

    Bluetooth::Create(Bluetooth::BasisMode::Classic, "esp hamster", bluetooth_to_apparatusQ);
    Apparatus::Create(bluetooth_to_apparatusQ);
    auto apparatus = Apparatus::Get();
    while (true) {
        Task::DelayMs(100);
    }
}
