#pragma once

#include <esp_event.h>
#include <tcpip_adapter.h>
#include <esp_wifi.h>

#include "asio.hpp"
#include "project_configs.hpp"
#include "esp_logger.hpp"
#include "task.hpp"
#include "wifi.hpp"
#include "queue.hpp"
#include "message.hpp"

template<typename MessageToMasterT>
class Communicator {
  public:
    using PortNumT  = unsigned short;
    using WQ        = Queue<MessageToMasterT>;
    using RQ        = Queue<MessageFromMaster>;
    using Byte      = uint8_t;
    using CommandsQ = Queue<MessageFromMaster::Data::Command>;

    enum MessageType {
        Confirmation = 1
    };

    Communicator(asio::ip::address_v4 ip_addr, PortNumT port_num, std::shared_ptr<ByteStreamBuffer> wQ, std::shared_ptr<CommandsQ> cmdQ) noexcept
      : masterIP{ std::move(ip_addr) }
      , currentSocketPort{ port_num }
      , wSB{std::move(wQ)}
      , commandsQ{ std::move(cmdQ) }
      , writeTask([this]() { WriteTask(); },
                  ProjCfg::Tasks::CommunicatorWriteStackSize,
                  ProjCfg::Tasks::CommunicatorWritePrio,
                  "write",
                  true)
      , readTask([this]() { ReadTask(); },
                 ProjCfg::Tasks::CommunicatorReadTaskSize,
                 ProjCfg::Tasks::CommunicatorReadPrio,
                 "read",
                 true)
    { }

    void run() noexcept
    {
        io_context = std::make_shared<asio::io_context>();
        socket     = std::make_shared<asio::ip::tcp::socket>(*io_context);

        auto working_port = ObtainWorkingPortNumber();

        if (not working_port) {
            console.LogError("Failed to retrieve port number!");
            std::terminate();
        }

        console.Log("Working port: " + std::to_string(*working_port));
        ConfirmOperation(true);

        socket->close();
        currentSocketPort = *working_port;
        endpoint          = std::make_shared<asio::ip::tcp::endpoint>(masterIP, currentSocketPort);
        auto err_code     = asio::error_code();

        try {
            socket->connect(*endpoint, err_code);
        } catch (asio::system_error &err) {
            console.OnFatalErrorTermination("error upon connection: " + err.code().message());
        }

        if (!err_code) {
            console.Log("Connected!");

            writeTask.Start();
            readTask.Start();
        }
        else {
            console.LogError("Error upon connection!:" + err_code.message());
        }
    }

  protected:
    [[noreturn]] void WriteTask() noexcept
    {
        asio::error_code err_code;
        while (true) {
            auto to_be_sent_bytes = wSB->Receive();

            if (to_be_sent_bytes == std::nullopt) {
                console.LogError("message to be sent is unhealthy!");
                continue;
            }

            asio::write(*socket, asio::buffer(to_be_sent_bytes->data(), to_be_sent_bytes->size()));
            console.Log("bytes sent to master!");
        }
    }
    [[noreturn]] void ReadTask() noexcept
    {
        auto one_byte_buffer = Byte{};
        auto main_buffer     = std::array<Byte, 256>{};
        auto err_code        = asio::error_code();

        while (true) {
            asio::read(*socket, asio::buffer(&one_byte_buffer, 1), err_code);
            if (err_code)
                console.OnFatalErrorTermination("error reading message type!" + err_code.message());

            auto message_type = static_cast<MessageFromMaster::MessageType>(one_byte_buffer);

            int args_num_to_read = -1;
            switch (message_type) {
            case MessageFromMaster::MessageType::COMMAND: {
                asio::read(*socket, asio::buffer(&one_byte_buffer, 1), err_code);

                if (err_code) {
                    console.OnFatalErrorTermination("error reading message number of args! " + err_code.message());
                }

                args_num_to_read = one_byte_buffer;
            } break;
            case MessageFromMaster::MessageType::RESULTS: {
                args_num_to_read = MessageFromMaster::Data::MeasurementsResult::SIZE_BYTES;
            } break;
            default: {
                console.OnFatalErrorTermination("Reading task found unknown message ID: " +
                                                std::to_string(one_byte_buffer));
            }
            }

            auto bytes_obtained = asio::read(*socket,
                                             asio::buffer(main_buffer.data(), main_buffer.size()),
                                             asio::transfer_exactly(args_num_to_read),
                                             err_code);

            if (err_code) {
                console.OnFatalErrorTermination("error during message body reading: " + err_code.message());
            }

            if (bytes_obtained != args_num_to_read) {
                console.LogError("Reading task::Reading from socket error!");
                continue;
            }

            switch (message_type) {
            case MessageFromMaster::MessageType::COMMAND: {
                try {
                    auto cmd = MessageFromMaster::Data::Command(
                      std::vector<Byte>(main_buffer.begin(), main_buffer.begin() + bytes_obtained));
                    console.Log("Command created! CommandID: " + std::to_string(cmd.commandID));

                    for (auto const &arg : cmd.arguments) {
                        console.Log("Arg: " + std::to_string(arg));
                    }

                    commandsQ->Send(cmd);
                } catch (const std::length_error &err) {
                    console.LogError("Error command creation! E: " + std::string(err.what()));
                    continue;
                }
            }; break;
            default: break;
            }
        }
    }

    std::optional<PortNumT> ObtainWorkingPortNumber() noexcept
    {
        endpoint      = std::make_shared<asio::ip::tcp::endpoint>(masterIP, currentSocketPort);
        auto err_code = asio::error_code();

        while (true) {
            try {
                socket->connect(*endpoint, err_code);
            } catch (asio::system_error &err) {
                console.LogError("Error during port obtainment: " + std::string(err.what()));
                continue;
            } catch (...) {
                console.LogError("Unknown error during port obtainment");
                std::terminate();
            }

            socketIsConnected = true;
            break;
        }

        auto result = Receive<int>();

        if (result.first != std::nullopt) {
            return *result.first;
        }
        else {
            console.LogError("Unsuccessful retrieval of working port number! Error: " +
                             std::string(result.second.message()));
            return std::nullopt;
        }
    }

    template<typename RType>
    std::pair<std::optional<RType>, asio::error_code> Receive() noexcept
    {
        if (not socketIsConnected)
            return { std::nullopt, std::make_error_code(std::errc::not_connected) };

        std::array<Byte, sizeof(RType)> buffer{};
        auto                            ec = asio::error_code();

        asio::read(*socket, asio::buffer(buffer.data(), buffer.size()), asio::transfer_exactly(sizeof(int)), ec);

        //        for (auto const &byte : buffer) {
        //            console.Log("Byte : " + std::to_string(byte));
        //        }

        if (ec) {
            console.LogError("Reception error: " + ec.message());
            return { std::nullopt, ec };
        }
        else {
            return { *reinterpret_cast<RType *>(buffer.data()), ec };
        }
    }
    bool ConfirmOperation(bool confirm_ok) noexcept
    {
        std::array<Byte, 2> buffer{ MessageType::Confirmation };
        buffer.at(1) = (confirm_ok == true) ? 1 : 0;

        asio::error_code ec;
        asio::write(*socket, asio::buffer(buffer.data(), buffer.size()), ec);

        if (!ec)
            return true;
        else {
            console.LogError("Confirmation write unsuccessful! Error: " + ec.message());
            return false;
        }
    }

  private:
    SmartLogger console{ "socket", ProjCfg::EnableLogForComponent::Socket };

    asio::ip::address_v4 masterIP;
    PortNumT             currentSocketPort;

    std::shared_ptr<asio::io_context>        io_context;
    std::shared_ptr<asio::ip::tcp::socket>   socket;
    std::shared_ptr<asio::ip::tcp::endpoint> endpoint;
    std::shared_ptr<ByteStreamBuffer>        wSB;

    std::shared_ptr<CommandsQ> commandsQ;

    Task writeTask;
    Task readTask;

    bool socketIsConnected = false;
};