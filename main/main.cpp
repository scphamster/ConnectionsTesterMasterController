#include "esp_log.h"
#include "driver/gpio.h"
#include <stdio.h>
// #include "../components/cpp_wrappers/gpio/include/gpio.hpp"
#include "gpio.hpp"

#include "freertos/FreeRTOS.h"
#include "FreeRTOS/task.h"

extern "C" void
app_main()
{
    Pin p1{0, Pin::Direction::Input };
    auto old_lvl = p1.GetLevel();

    while(1) {
        auto current_lvl = p1.GetLevel();

        if (current_lvl != old_lvl) {
            old_lvl = current_lvl;
            ESP_LOGI("DBG", "PinLvl Changed, lvl=%i", static_cast<int>(current_lvl));
            vTaskDelay(pdMS_TO_TICKS(50));
        }

        vTaskDelay(1);
    }
}
