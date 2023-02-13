#pragma once
#include <memory>

#include "task.hpp"
#include "reset_reason_notifier.hpp"
#include "esp_logger.hpp"

class Application {
  public:
    using IPv4 = uint32_t;

    static void Run() noexcept { _this = std::shared_ptr<Application>(new Application()); }

  protected:
    void Initialize() noexcept
    {
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
    }

    void ConnectWireless() noexcept
    {
        tcpip_adapter_ip_info_t ip_info;
        esp_err_t               err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        if (err != ESP_OK) {
            console.OnFatalErrorTermination("TCPIP Adapter: Failed to get ip info. Error: " +
                                            std::string(esp_err_to_name(err)));
        }

        masterIP = asio::ip::address_v4::from_string(ip4addr_ntoa(&ip_info.gw));
        console.Log("Master's IP: " + masterIP.to_string());
    }

    [[noreturn]] void MainTask() noexcept
    {
        ResetReasonNotifier rrNotifier{};
        rrNotifier.Notify();

        Initialize();
        ConnectWireless();

        auto commandQ = std::make_shared<Queue<MessageFromMaster>>(10);
        auto writeSB  = std::make_shared<ByteStreamBuffer>(256);

        auto communicator =
          std::make_shared<Communicator<MessageToMaster>>(masterIP, ProjCfg::Socket::EntryPortNumber, writeSB, commandQ);
        communicator->run();

        auto bluetooth_to_apparatusQ = std::make_shared<Queue<char>>(100, "from bluetooth");
        Bluetooth::Create(Bluetooth::BasisMode::Classic, "esp hamster", bluetooth_to_apparatusQ);
        Apparatus::Create(bluetooth_to_apparatusQ, communicator);
        auto apparatus = Apparatus::Get();

        auto msg_to_master =
          PinConnectivity{ PinConnectivity::PinAffinityAndId{ 37, 1 },
                           std::vector<PinConnectivity::PinConnectionData>{ PinConnectivity::PinConnectionData{
                                                                              PinConnectivity::PinAffinityAndId{ 38, 1 },
                                                                              10,
                                                                            },
                                                                            PinConnectivity::PinConnectionData{
                                                                              PinConnectivity::PinAffinityAndId{ 39, 1 },
                                                                              100,
                                                                            } } };
        auto serialized = msg_to_master.Serialize();

        auto ack = Confirmation(Confirmation::Answer::CommandAcknowledge).Serialize();
        auto cmd_good = Confirmation(Confirmation::Answer::CommandPerformanceSuccess).Serialize();

        while (true) {
            auto cmd = commandQ->Receive();
            writeSB->Send(ack);
            Task::DelayMs(100);
            writeSB->Send(cmd_good);
        }
    }

  private:
    Application()
      : mainTask([this]() { MainTask(); }, ProjCfg::MainStackSize, ProjCfg::MainPrio, "Main", false)
    { }
    static std::shared_ptr<Application> _this;

    SmartLogger console{ "Main", ProjCfg::EnableLogForComponent::Main };
    Task        mainTask;

    asio::ip::address_v4 masterIP;
};