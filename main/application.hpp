#pragma once
#include <memory>

#include "task.hpp"
#include "reset_reason_notifier.hpp"
#include "esp_logger.hpp"

class Application {
  public:
    using IPv4 = uint32_t;
    using Comm = Communicator<MessageToMaster>;

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

        auto fromMasterMsgQ = std::make_shared<Queue<MessageFromMaster>>(10);
        auto toMasterSB     = std::make_shared<ByteStreamBuffer>(256);

        communicator = std::make_shared<Comm>(masterIP, ProjCfg::Socket::EntryPortNumber, toMasterSB, fromMasterMsgQ);
        communicator->run();

        auto bluetooth_to_apparatusQ = std::make_shared<Queue<char>>(100, "from bluetooth");
        Bluetooth::Create(Bluetooth::BasisMode::Classic, "esp hamster", bluetooth_to_apparatusQ);

        Apparatus::Create(bluetooth_to_apparatusQ, communicator);
        auto apparatus = Apparatus::Get();

        commandManagerTask.Start();
        mainTask.Stop();
    }

    [[noreturn]] void CommandManagerTask() noexcept
    {
        auto from_master_q = communicator->GetFromMasterCommandsQ();
        auto to_master_sb  = communicator->GetToMasterSB();

        while (true) {
            auto msg = from_master_q->Receive();




        }
    }

  private:
    Application()
      : mainTask([this]() { MainTask(); }, ProjCfg::MainStackSize, ProjCfg::MainPrio, "Main", false)
    { }
    static std::shared_ptr<Application> _this;

    SmartLogger          console{ "Main", ProjCfg::EnableLogForComponent::Main };
    Task                 mainTask;
    Task                 commandManagerTask{ [this]() { CommandManagerTask(); },
                             ProjCfg::Tasks::CommandManagerStackSize,
                             ProjCfg::Tasks::CommandManagerPrio,
                             "CommandManager",
                             true };
    asio::ip::address_v4 masterIP;

    std::shared_ptr<Comm> communicator;
};