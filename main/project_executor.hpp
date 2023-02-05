#pragma once
#include <memory>
#include "task.hpp"
#include "reset_reason_notifier.hpp"

class Application {
  public:
    static void                         Create() noexcept { _this = std::shared_ptr<Application>(new Application()); }
    static std::shared_ptr<Application> Get() noexcept
    {
        if (not _this) {
            EspLogger::LogError("Application",
                                "Application was not created yet, invoke \"Create\" before trying to get instance");

            Task::DelayMs(2000);
            std::terminate();
        }
        return _this;
    }

  protected:
    [[noreturn]] void MainTask() noexcept
    {
        ResetReasonNotifier rrNotifier{};
        rrNotifier.Notify();

        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        esp_netif_init();
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        auto connection_result = example_connect();
        if (connection_result != ESP_OK) {
            ESP_LOGE("MAIN", "No WIFI Connection obtained! Restarting!");
            vTaskDelay(pdMS_TO_TICKS(2000));
            std::terminate();
        }

        tcpip_adapter_ip_info_t ip_info;
        esp_err_t               err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        if (err == ESP_OK) {
            printf("AP IP: %s\n", ip4addr_ntoa(&ip_info.gw));
        }
        else {
            printf("Failed to get AP IP\n");
        }

        auto write_queue = std::make_shared<Queue<Message>>(10);
        auto communicator =
          std::make_shared<Communicator>(ip4addr_ntoa(&ip_info.gw), ProjCfg::Socket::EntryPortNumber, write_queue);
        communicator->run();

        auto bluetooth_to_apparatusQ = std::make_shared<Queue<char>>(100, "from bluetooth");
        Bluetooth::Create(Bluetooth::BasisMode::Classic, "esp hamster", bluetooth_to_apparatusQ);
        Apparatus::Create(bluetooth_to_apparatusQ, communicator);
        auto apparatus = Apparatus::Get();

        Message new_msg;
        while (true) {
            Task::DelayMs(500);
            write_queue->SendImmediate(new_msg);
        }
    }

  private:
    Application()
      : mainTask([this]() { MainTask(); }, ProjCfg::MainStackSize, ProjCfg::MainPrio, "Main", false)
    { }
    static std::shared_ptr<Application> _this;

    Task mainTask;
};