#pragma once

#include <esp_event.h>
#include <tcpip_adapter.h>
#include <esp_wifi.h>

#include "asio.hpp"
#include "../../proj_cfg/project_configs.hpp"
#include "esp_logger.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "message.hpp"

template<typename MessageToMasterT>
class Communicator {
  public:
    using PortNumT    = unsigned short;
    using Byte        = uint8_t;
    using FromMasterQ = Queue<MessageFromMaster>;

    enum MessageType {
        Confirmation = 1
    };

    Communicator(asio::ip::address_v4              ip_addr,
                 PortNumT                          port_num,
                 std::shared_ptr<ByteStreamBuffer> toMasterSB,
                 std::shared_ptr<FromMasterQ>      fromMasterQ) noexcept
      : masterIP{ std::move(ip_addr) }
      , currentSocketPort{ port_num }
      , toMasterSB{ std::move(toMasterSB) }
      , fromMasterCommandsQ{ std::move(fromMasterQ) }
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

    std::shared_ptr<FromMasterQ> GetFromMasterCommandsQ() noexcept { return fromMasterCommandsQ; }

    std::shared_ptr<ByteStreamBuffer> GetToMasterSB() noexcept { return toMasterSB; }

    void SetImmediateAutoResponse(CommandStatus &&response) noexcept { *immediateAutoResponse = std::move(response); }
    void UnsetNotInitializedFlagResponse() noexcept { immediateAutoResponse = std::nullopt; }

  protected:
    [[noreturn]] void WriteTask() noexcept
    {
        asio::error_code err_code;
        while (true) {
            auto bytes_to_be_sent = toMasterSB->Receive();

            if (bytes_to_be_sent == std::nullopt) {
                console.LogError("message to be sent is unhealthy!");
                continue;
            }

            auto message_size = bytes_to_be_sent->size();

            asio::write(*socket, asio::buffer(&message_size, sizeof(message_size)));
            asio::write(*socket, asio::buffer(bytes_to_be_sent->data(), bytes_to_be_sent->size()));
            console.Log(std::to_string(message_size) + " bytes sent to master!");
        }
    }
    [[noreturn]] void ReadTask() noexcept
    {
        auto message_size_buffer = int{};
        auto main_buffer         = std::array<Byte, 256>{};
        auto err_code            = asio::error_code();

        while (true) {
            asio::read(*socket, asio::buffer(&message_size_buffer, sizeof(message_size_buffer)), err_code);
            if (err_code)
                console.OnFatalErrorTermination("error reading message size!" + err_code.message());

            auto message_size   = message_size_buffer;
            auto bytes_read_num = asio::read(*socket,
                                             asio::buffer(main_buffer.data(), main_buffer.size()),
                                             asio::transfer_exactly(message_size),
                                             err_code);

            if (message_size != bytes_read_num) {
                console.LogError("Message size is " + std::to_string(bytes_read_num) +
                                 " and is different from expected: " + std::to_string(message_size));
                continue;
            }

            if (err_code) {
                console.OnFatalErrorTermination("error during message body reading: " + err_code.message());
            }

            try {
                if (immediateAutoResponse) {
                    console.Log("Answering \"Im Initializing\" to master!");
                    toMasterSB->Send(immediateAutoResponse->Serialize(), pdMS_TO_TICKS(100));
                    continue;
                }

                auto msg = MessageFromMaster(std::vector<Byte>(main_buffer.begin(), main_buffer.end()));
                console.Log("New successful creation of messageFromMaster!");
                fromMasterCommandsQ->Send(msg);
            } catch (const std::invalid_argument &exception) {
                console.LogError("Invalid argument exception when creating message from master: " +
                                 std::string(exception.what()));


            }
            catch(std::system_error &e) {
                console.LogError(e.what());
            }
            catch (...) {
                console.LogError("Unknown error when creating MessageFromMaster");
                continue;
            }
        }
    }

    std::optional<PortNumT> ObtainWorkingPortNumber() noexcept
    {
        endpoint      = std::make_shared<asio::ip::tcp::endpoint>(masterIP, currentSocketPort);
        auto err_code = asio::error_code();

        while (true) {
            try {
                console.Log("Connecting to standard port: " + std::to_string(currentSocketPort));
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
    std::shared_ptr<ByteStreamBuffer>        toMasterSB;

    std::shared_ptr<FromMasterQ> fromMasterCommandsQ;

    Task writeTask;
    Task readTask;

    std::optional<CommandStatus> immediateAutoResponse{ CommandStatus(CommandStatus::Answer::DeviceIsInitializing) };

    bool socketIsConnected = false;
};