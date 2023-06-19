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
#include "boards_manager.hpp"
#include "board.hpp"
#include "message.hpp"

class Application {
  public:
    using IPv4 = uint32_t;
    using Comm = Communicator<MessageToMaster>;

    static void Run() noexcept { _this = std::shared_ptr<Application>(new Application()); }

  protected:
    static void Initialize() noexcept
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
        auto toMasterSB     = std::make_shared<ByteStreamBuffer>(2048);

        communicator = std::make_shared<Comm>(masterIP, ProjCfg::Socket::EntryPortNumber, toMasterSB, fromMasterMsgQ);
        communicator->run();

        Apparatus::Create(communicator);
        apparatus = Apparatus::Get();

        commandManagerTask.Start();

        mainTask.Stop();
    }

    [[noreturn]] void CommandManagerTask() noexcept
    {
        auto from_master_q = communicator->GetFromMasterCommandsQ();
        auto to_master_sb  = communicator->GetToMasterSB();

        while (not apparatus->BoardsSearchPerformed()) {
            Task::DelayMs(100);
        }

        communicator->UnsetNotInitializedFlagResponse();

        while (true) {
            auto msg = from_master_q->Receive();
            if (msg == std::nullopt) {
                console.LogError("CMT: Message arrived is null");
                continue;
            }

            using ID = MessageFromMaster::Command::ID;

            auto cmd_id = msg->GetCommandID();
            switch (cmd_id) {
            case ID::GetBoards: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                console.Log("FromMasterCMD: Get boards " + std::to_string(msg->cmd.getBoards.performRescan));

                auto boards_info = apparatus->GetBoards(msg->cmd.getBoards.performRescan);
                if (boards_info == std::nullopt) {
                    console.LogError("Get boards command failed!");
                    continue;
                }

                auto bytes       = BoardsInfo(std::move(*boards_info)).Serialize();
                int  counter     = 0;
                for (auto const &byte : bytes) {
                    console.Log(std::to_string(counter) + ":" + std::to_string(byte));
                    counter++;
                }
                to_master_sb->Send(bytes);
                console.Log("CMT: response with boards sent!");
            } break;
            case ID::MeasureAll: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                console.Log("FromMasterCMD: MeasureAll");

                auto measured_voltages = apparatus->MeasureAll();

                if (measured_voltages == std::nullopt) {
                    to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandPerformanceFailure).Serialize());
                    continue;
                }
                else {
                    console.Log("Measured All voltages, sending to master!");
                    auto send_result = to_master_sb->Send(measured_voltages->Serialize());
                    console.Log("All voltages send success?: " + std::to_string(send_result));
                }
            } break;
            case ID::CheckConnections: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                console.Log("FromMasterCMD: CheckAllConnections");

                if (msg->cmd.checkConnections.measureAll) {
                    apparatus->CheckAllConnections();
                }
                else {
                    console.Log("Check connection received");

                    apparatus->CheckConnection(Board::PinAffinityAndId{ msg->cmd.checkConnections.boardAffinity,
                                                                        msg->cmd.checkConnections.pinNumber });
                }

            } break;
            case ID::DataLinkKeepAlive: {
                to_master_sb->Send(KeepAlive().Serialize());
                console.Log("KeepAlive message from master, sending keepalive back!");
            } break;
            case ID::EnableOutputForPin: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                apparatus->EnableOutputForPin(msg->cmd.enableOutputForPin.pinAffinityAndId);
            } break;

            case ID::DisableOutput: {
                to_master_sb->Send(CommandStatus(CommandStatus::Answer::CommandAcknowledge).Serialize());
                apparatus->DisableOutput();
            } break;

            case ID::Dummy: to_master_sb->Send(Dummy{}.Serialize()); break;
            default: console.LogError("Unhandled command arrived! " + std::to_string(ToUnderlying(cmd_id))); break;
            }
        }
    }

  private:
    Application() = default;

    static std::shared_ptr<Application> _this;

    Logger console{ "Main", ProjCfg::EnableLogForComponent::Main };
    Task   mainTask{ [this]() { MainTask(); }, ProjCfg::MainStackSize, ProjCfg::MainPrio, "Main", false };
    Task   commandManagerTask{ [this]() { CommandManagerTask(); }, ProjCfg::Tasks::CommandManagerStackSize,
                             ProjCfg::Tasks::CommandManagerPrio, "CommandManager",
                             ProjCfg::Tasks::DefaultTasksCore,   true };

    asio::ip::address_v4 masterIP;

    std::shared_ptr<Comm>      communicator;
    std::shared_ptr<Apparatus> apparatus;
};