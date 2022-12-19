#pragma once
#include <memory>
#include <vector>

#include "project_configs.hpp"
#include "board_controller.hpp"
#include "data_link.hpp"
#include "esp_logger.hpp"
#include "iic.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "bluetooth.hpp"

class Apparatus {
  public:
    using Byte         = uint8_t;
    using BoardAddrT   = Byte;
    using PinsVoltages = Board::PinsVoltages;
    using QueueT       = Queue<PinsVoltages>;

    void static Create(std::shared_ptr<Queue<char>> input_queue) noexcept
    {
        _this = std::shared_ptr<Apparatus>{ new Apparatus{ std::move(input_queue) } };
    }
    auto static Get() noexcept
    {
        auto console = EspLogger::Get();

        if (_this == nullptr) {
            console->LogError("Apparatus was not created yet, invoke Apparatus::Create before trying to Get instance");
            std::terminate();
        }

        return _this;
    }

  protected:
    enum class UserCommand {
        MeasureAll
    };
    void Init() noexcept { FindAllConnectedBoards(); }
    void FindAllConnectedBoards() noexcept
    {
        auto constexpr start_address = 0x20;
        auto constexpr last_address  = 0x50;

        for (auto addr = start_address; addr <= last_address; addr++) {
            auto board_found = i2c->CheckIfSlaveWithAddressIsOnLine(addr);

            if (board_found) {
                console.Log("Board with address: " + std::to_string(addr) + " was found");
                ioBoards.emplace_back(std::make_shared<Board>(addr, voltageTablesQueue));
                boardsSemaphores.push_back(ioBoards.back()->GetStartSemaphore());
            }
        }

        if (ioBoards.size() == 0)
            console.LogError("No boards found, check was performed between addresses: " + std::to_string(start_address) +
                             " and " + std::to_string(last_address));

        for (auto const &board : ioBoards) {
            auto [read_succeeded, counter_value] = board->GetBoardCounterValue();
            if (not read_succeeded) {
                console.LogError("Board with address: " + std::to_string(board->GetAddress()) +
                                 " did not successfully answered to CounterValue request");
            }
            else {
                console.Log("Board with address: " + std::to_string(board->GetAddress()) +
                            " properly answered for InternalCounter Command, value = " + std::to_string(counter_value));
            }
        }
    }

    void MeasureAll() noexcept
    {
        for (auto const &semaphore : boardsSemaphores) {
            semaphore->Give();
        }

        std::vector<PinsVoltages> voltageTable;

        for (auto const &dummy : ioBoards) {
            voltageTable.push_back(voltageTablesQueue->Receive());
        }

        console.Log("Voltage table arrived, size: " + std::to_string(voltageTable.size()));
        for (auto const &table : voltageTable) {
            console.Log("Voltage table from board: " + std::to_string(table.boardAddress));
            bluetooth->Write("Voltage table from board: " + std::to_string(table.boardAddress) + '\n');

            auto pin_num = 0;
            for (auto const &value : table.voltagesArray) {
                console.Log(std::to_string(pin_num) + " V=" + std::to_string(value));
                bluetooth->Write(std::to_string(pin_num) + " V=" + std::to_string(value) + '\n');

                pin_num++;
            }
        }
    }
    UserCommand WaitForUserCommand() noexcept
    {   //todo: make full parser
        while (true) {
            auto value = inputQueue->Receive();

            if (value == 'a')
                return UserCommand::MeasureAll;
        }
    }

    // tasks
    void MeasurementsTask() noexcept
    {
        while (true) {
            auto command = WaitForUserCommand();

            switch (command) {
            case UserCommand::MeasureAll: MeasureAll(); break;
            default: break;
            }

            Task::DelayMs(2000);
        }
    }

  private:
    Apparatus(std::shared_ptr<Queue<char>> input_queue)
      : console{ "Main", ProjCfg::ComponentLogSwitch::Main }
      , voltageTablesQueue{ std::make_shared<QueueT>(10) }
      , inputQueue{ std::move(input_queue) }
      , bluetooth{ Bluetooth::Get() }
      , measurementsTask{ [this]() { MeasurementsTask(); },
                          static_cast<size_t>(ProjCfg::Tasks::MainMeasurementsTaskStackSize),
                          static_cast<size_t>(ProjCfg::Tasks::MainMeasurementsTaskPrio),
                          "meas",
                          true }
    {
        IIC::Create(IIC::Role::Master,
                    ProjCfg::BoardsConfigs::SDA_Pin,
                    ProjCfg::BoardsConfigs::SCL_Pin,
                    ProjCfg::BoardsConfigs::IICSpeedHz);
        i2c = IIC::Get();

        Init();

        measurementsTask.Start();
    }

    std::shared_ptr<Apparatus> static _this;

    SmartLogger                             console;
    std::shared_ptr<IIC>                    i2c = nullptr;
    std::vector<std::shared_ptr<Board>>     ioBoards;
    std::vector<std::shared_ptr<Semaphore>> boardsSemaphores;
    std::shared_ptr<QueueT>                 voltageTablesQueue;
    std::shared_ptr<Queue<char>>            inputQueue;
    std::shared_ptr<Bluetooth>              bluetooth;
    Task                                    measurementsTask;
};