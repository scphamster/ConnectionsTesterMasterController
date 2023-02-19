#pragma once
#include <memory>
#include <nvs_flash.h>
#include <protocol_examples_common.h>

#include "asio.hpp"

#include "task.hpp"
#include "queue.hpp"

#include "../proj_cfg/project_configs.hpp"
#include "esp_logger.hpp"
#include "bluetooth.hpp"

#include "reset_reason_notifier.hpp"
#include "main_apparatus.hpp"
#include "board_controller.hpp"
#include "message.hpp"

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
    }

    void ConnectWireless() noexcept
    {
        auto connection_result = example_connect();
        if (connection_result != ESP_OK) {
            console.OnFatalErrorTermination("No WIFI Connection obtained! Restarting!");
        }

        tcpip_adapter_ip_info_t ip_info;
        esp_err_t               err = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
        if (err != ESP_OK) {
            console.OnFatalErrorTermination("TCPIP Adapter: Failed to get ip info. Error: " +
                                            std::string(esp_err_to_name(err)));
        }

        masterIP = asio::ip::address_v4::from_string(ip4addr_ntoa(&ip_info.gw));
        console.Log("Master's IP: " + masterIP.to_string());
    }

    void MainTask() noexcept
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
        apparatus = Apparatus::Get();

        commandManagerTask.Start();

        mainTask.Stop();
    }

    [[noreturn]] void CommandManagerTask() noexcept
    {
        auto from_master_q = communicator->GetFromMasterCommandsQ();
        auto to_master_sb  = communicator->GetToMasterSB();

        while (not apparatus->BoardsHaveBeenChecked()) {
            Task::DelayMs(100);
        }

        communicator->UnsetNotInitializedFlagResponse();

        while (true) {
            auto msg = from_master_q->Receive();
            if (msg == std::nullopt) {
                console.LogError("CMT: Message arrived is null");
                continue;
            }

            using ID    = MessageFromMaster::Command::ID;
            auto cmd_id = msg->GetCommandID();
            switch (cmd_id) {
            case ID::GetBoards: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                auto boards_info = apparatus->GetBoards();
                if (boards_info == std::nullopt) {
                    to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandPerformanceFailure).Serialize());
                    break;
                }
                else {
                    auto bytes   = BoardsInfo(std::move(*boards_info)).Serialize();
                    int  counter = 0;
                    for (auto const &byte : bytes) {
                        console.Log(std::to_string(counter) + ":" + std::to_string(byte));
                        counter++;
                    }
                    to_master_sb->Send(std::move(bytes));
                    console.Log("CMT: response with boards sent!");
                }
            } break;
            case ID::MeasureAll: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                auto result = apparatus->MeasureAll();
                if (result == std::nullopt) {
                    to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandPerformanceFailure).Serialize());
                    continue;
                }
                else {
                    to_master_sb->Send(result->Serialize());
                }
            } break;
            case ID::CheckConnections: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                apparatus->CheckConnections();
            } break;
            default: console.LogError("Unhandled command arrived! " + std::to_string(ToUnderlying(cmd_id))); break;
            }
        }
    }

  private:
    static std::shared_ptr<Application> _this;

    SmartLogger console{ "Main", ProjCfg::EnableLogForComponent::Main };
    Task        mainTask{ [this]() { MainTask(); }, ProjCfg::MainStackSize, ProjCfg::MainPrio, "Main", false };
    Task        commandManagerTask{ [this]() { CommandManagerTask(); },
                             ProjCfg::Tasks::CommandManagerStackSize,
                             ProjCfg::Tasks::CommandManagerPrio,
                             "CommandManager",
                             true };

    asio::ip::address_v4 masterIP;

    std::shared_ptr<Comm>      communicator;
    std::shared_ptr<Apparatus> apparatus;
};