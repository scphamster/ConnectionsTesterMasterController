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
#include "string_parser.hpp"

class Apparatus {
  public:
    using Byte         = uint8_t;
    using BoardAddrT   = Byte;
    using PinsVoltages = Board::PinsVoltages;
    using QueueT       = Queue<PinsVoltages>;
    using PinNumT      = Board::PinNumT;
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
        MeasureAll,
        SetPinVoltage
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

    void SetPinVoltage(PinNumT pin, bool new_pin_state) noexcept
    {
        // todo: replace magic numbers
        auto board_num    = pin / 32;
        auto pin_at_board = pin % 32;

        if (board_num + 1 > ioBoards.size()) {
            std::string err_msg = "requested pin number: " + std::to_string(pin) +
                                  " which requires board number (count from 1) = " + std::to_string(board_num + 1) +
                                  " but there are only " + std::to_string(ioBoards.size()) + " boards found\n";

            console.LogError(err_msg);
            bluetooth->Write(err_msg);

            return;
        }

        // todo: use new_level variable
        ioBoards.at(board_num)->SetVoltageAtPin(pin_at_board);
    }

    void FindConnections(std::vector<PinNumT> for_pins) noexcept
    {
        if (for_pins.size() == 0)
            return;

        for (auto pin : for_pins) {
            SetPinVoltage(pin, true);

            auto all_voltages = MeasureAll();

            std::string connected_to_this;

            auto pin_counter = 0;

            for (auto voltages_for_board : all_voltages) {
                for (auto voltage : voltages_for_board.voltagesArray) {
                    if (voltage > 0) {
                        connected_to_this.append(std::to_string(pin_counter) + ' ');
                    }

                    pin_counter++;
                }
            }

            console.Log("Connected to " + std::to_string(pin) + ' ' + connected_to_this);
        }
    }

    std::vector<PinsVoltages> MeasureAll() noexcept
    {
        for (auto const &semaphore : boardsSemaphores) {
            semaphore->Give();
        }

        std::vector<PinsVoltages> voltageTable;

        for (auto const &dummy : ioBoards) {
            voltageTable.push_back(*voltageTablesQueue->Receive());
        }

        console.Log("Voltage table arrived, size: " + std::to_string(voltageTable.size()));
        for (auto const &table : voltageTable) {
//            console.Log("Voltage table from board: " + std::to_string(table.boardAddress));
//            bluetooth->Write("Voltage table from board: " + std::to_string(table.boardAddress) + '\n');

            auto pin_num = 0;
            for (auto const &value : table.voltagesArray) {
                console.Log(std::to_string(pin_num) + " V=" + std::to_string(value));
                bluetooth->Write(std::to_string(pin_num) + " V=" + std::to_string(value) + '\n');

                pin_num++;
            }
        }

        return voltageTable;
    }
    std::pair<UserCommand, int> WaitForUserCommand() noexcept
    {   // todo: make full parser
        while (true) {
            std::string cmd;
            cmd += *inputQueue->Receive();

            for (;;) {
                auto result = inputQueue->Receive(pdMS_TO_TICKS(10));
                if (result) {
                    cmd += std::move(*result);
                }
                else
                    break;
            }

            auto words = StringParser::GetWords(cmd);

            console.Log("new command arrived");
            for (auto const &word : words) {
                console.Log(word);
            }

            if (words.at(0) == "set") {
                int argument;

                try {
                    argument = std::stoi(words.at(1));
                } catch (...) {
                    console.LogError("invalid argument: " + words.at(1) + ", exception: " /*+ ex.what()*/);
                }

                return { UserCommand::SetPinVoltage, argument };
            }
            else if (words.at(0) == "all") {
                return { UserCommand::MeasureAll, 0 };
            }
        }
    }

    // tasks
    void MeasurementsTask() noexcept
    {
        while (true) {
            auto [command, args] = WaitForUserCommand();

            switch (command) {
                //            case UserCommand::MeasureAll: MeasureAll(); break;
            case UserCommand::MeasureAll:
                FindConnections(std::vector<PinNumT>{ 1, 2, 3 });
                break;
                //            case UserCommand::SetPinVoltage: SetPinVoltage(args, true);
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