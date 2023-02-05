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

class Communicator {
  public:
    using PortNumT = unsigned short;
    using QueueT   = Queue<Message>;
    using Byte     = uint8_t;

    enum MessageType {
        Confirmation = 1
    };

    Communicator(std::string ip_addr, PortNumT port_num, std::shared_ptr<QueueT> write_queue) noexcept
      : ip{ std::move(ip_addr) }
      , port{ port_num }
      , wQueue{ std::move(write_queue) }
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

        console.Log("Obtained port number: " + std::to_string(*working_port));
        ConfirmOperation(true);

        socket->close();
        port = *working_port;

        endpoint = std::make_shared<asio::ip::tcp::endpoint>(asio::ip::address::from_string(ip), port);

        auto err_code = asio::error_code();
        //        InitWiFi();

        try {
            socket->connect(*endpoint, err_code);
        } catch (asio::system_error &err) {
            console.LogError("error upon connection: " + err.code().message());

            vTaskDelay(pdMS_TO_TICKS(1000));
            std::terminate();
        }

        if (!err_code) {
            console.Log("Connected!(probably)");

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
        std::string      some_string = "12345";
        while (true) {
            auto new_msg = wQueue->Receive();

            auto as_array = reinterpret_cast<std::array<Byte, sizeof(Message)> *>(&(*new_msg));

            console.Log("Sending...");
            std::for_each(as_array->begin(), as_array->end(), [this](const auto &item) {
                console.Log("Byte: " + std::to_string(item));
            });

            asio::write(*socket, asio::buffer(as_array->data(), as_array->size()), err_code);
            if (!err_code) {
                console.Log("success upon write");
            }
            else {
                console.Log("error upon write!:");
            }
        }
    }
    [[noreturn]] void ReadTask() noexcept
    {
        auto buf = std::array<char, 200>{};

        while (true) {
            auto bytes_obtained = socket->read_some(asio::buffer(buf.data(), buf.size()));

            Task::DelayMs(10);
        }
    }

    std::optional<PortNumT> ObtainWorkingPortNumber() noexcept
    {
        endpoint      = std::make_shared<asio::ip::tcp::endpoint>(asio::ip::address::from_string(ip), port);
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

        std::array<Byte, sizeof(RType) * 3> buffer{};
        auto                                ec = asio::error_code();

        asio::read(*socket, asio::buffer(buffer.data(), buffer.size()), asio::transfer_exactly(sizeof(int)), ec);

        for (auto const &byte : buffer) {
            console.Log("Byte : " + std::to_string(byte));
        }

        if (ec) {
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

    std::string ip;
    PortNumT    port;

    std::shared_ptr<asio::io_context>        io_context;
    std::shared_ptr<asio::ip::tcp::socket>   socket;
    std::shared_ptr<asio::ip::tcp::endpoint> endpoint;
    std::shared_ptr<QueueT>                  wQueue;
    Task                                     writeTask;
    Task                                     readTask;

    bool socketIsConnected = false;
};