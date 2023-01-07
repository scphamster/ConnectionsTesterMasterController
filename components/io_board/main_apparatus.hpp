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
    using Byte               = uint8_t;
    using BoardAddrT         = Byte;
    using PinsVoltages       = Board::PinsVoltages;
    using OutputVoltageLevel = Board::OutputVoltage;
    using QueueT             = Queue<PinsVoltages>;
    using PinNumT            = Board::PinNumT;
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
        EnableOutputForPin,
        CheckConnections,
        GetAllBoardsIds,
        Unknown
    };
    struct SetPinVoltageCmd {
        enum SpecialPinConfigurations : Byte {
            DisableAll = 254
        };
    };

    void Init() noexcept { FindAllConnectedBoards(); }

    void FindAllConnectedBoards() noexcept
    {
        auto constexpr start_address = 0x20;
        auto constexpr last_address  = 0x50;

        for (auto addr = start_address; addr <= last_address; addr++) {
            auto board_found = i2c->CheckIfSlaveWithAddressIsOnLine(addr);

            if (board_found) {
                console.Log("board with address:" + std::to_string(addr) + " was found");
                ioBoards.emplace_back(std::make_shared<Board>(addr, pinsVoltagesResultsQ));
                boardsSemaphores.push_back(ioBoards.back()->GetStartSemaphore());
            }
        }

        if (ioBoards.size() == 0)
            console.LogError("No boards found, check was performed between addresses: " + std::to_string(start_address) +
                             " and " + std::to_string(last_address));

        Task::DelayMs(200);

        for (auto const &board : ioBoards) {
            auto counter_value = board->GetBoardCounterValue();
            if (not counter_value) {
                console.LogError("Board with address: " + std::to_string(board->GetAddress()) +
                                 " has not properly answered to CounterValue request");
            }
            else {
                console.Log("Board with address: " + std::to_string(board->GetAddress()) +
                            " properly answered for InternalCounter Command, value = " + std::to_string(*counter_value));
            }
        }
    }
    void SendAllBoardsIds() noexcept
    {
        std::string response = "HW dummyarg -> ";

        for (auto board : ioBoards) {
            response.append(std::to_string(board->GetAddress()) + ' ');
        }

        response.append("END\n");

        bluetooth->Write(response);
        console.Log("response sent: " + response);
    }
    void EnableOutputForPin(PinNumT pin) noexcept
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
    void SetOutputVoltageValue(OutputVoltageLevel level) noexcept
    {
        for (auto const &board : ioBoards) {
            board->SetOutputVoltageValue(level);
        }
    }
    void FindAllConnectionsForBoard(std::shared_ptr<Board> board)
    {
        constexpr auto pin_count_at_board = Board::pinCount;

        for (PinNumT pin = 0; pin < pin_count_at_board; pin++) {
            board->SetVoltageAtPin(pin);
            Task::DelayMs(1);

            auto voltage_tables_from_all_boards = MeasureAll();
            if (voltage_tables_from_all_boards == std::nullopt) {
                console.LogError("Unsuccessful!");
                return;
            }

            std::string bt_string = "CONNECT " + std::to_string(board->GetAddress()) + ':' + std::to_string(pin) + " -> ";

            for (const auto &voltage_table_from_board : *voltage_tables_from_all_boards) {
                std::string board_affinity = std::to_string(voltage_table_from_board.boardAddress);

                auto pin_counter = 0;
                for (auto voltage : voltage_table_from_board.voltagesArray) {
                    if (voltage > 0) {
                        bt_string.append(board_affinity + ':' + std::to_string(pin_counter) + ' ');
                    }

                    pin_counter++;
                }
            }

            bt_string.append("END\n");

            console.Log(bt_string);
            bluetooth->Write(bt_string);

            Task::DelayMs(3);
        }
    }
    void FindAllConnections() noexcept
    {
        console.Log("Executing command: FindAllConnections");

        for (auto board : ioBoards) {
            FindAllConnectionsForBoard(board);
        }
    }
    void FindConnectionsAtBoardForPin(BoardAddrT board_address, PinNumT pin)
    {
        if (pin > Board::pinCount) {
            console.LogError("requested pin number is higher than pin count at one board, requested pin: " +
                             std::to_string(pin));
            return;
        }

        auto board_it = std::find_if(ioBoards.begin(), ioBoards.end(), [board_address](auto board) {
            return board->GetAddress() == board_address;
        });

        if (board_it == ioBoards.end()) {
            console.LogError("No board found with address: " + std::to_string(board_address));
            return;
        }

        (*board_it)->SetVoltageAtPin(pin);

        auto voltage_tables_from_all_boards = MeasureAll();
        if (voltage_tables_from_all_boards == std::nullopt) {
            console.LogError("Unsuccessful");
            return;
        }

        PrintAllVoltagesFromTable(*voltage_tables_from_all_boards);

        std::string bt_string =
          "CONNECT " + std::to_string((*board_it)->GetAddress()) + ':' + std::to_string(pin) + " -> ";

        for (const auto &voltage_table_from_board : *voltage_tables_from_all_boards) {
            std::string board_affinity = std::to_string(voltage_table_from_board.boardAddress);

            auto pin_counter = 0;
            for (auto voltage : voltage_table_from_board.voltagesArray) {
                if (voltage > 0) {
                    bt_string.append(board_affinity + ':' + std::to_string(pin_counter) + ' ');
                }

                pin_counter++;
            }
        }

        bt_string.append("END\n");
        console.Log(bt_string);
    }
    std::optional<std::vector<PinsVoltages>> MeasureAll() noexcept
    {
        StartVoltageMeasurementOnAllBoards();

        std::vector<PinsVoltages> all_boards_voltages;

        for (auto board = 0; board < ioBoards.size(); board++) {
            auto voltage_table = pinsVoltagesResultsQ->Receive(pdMS_TO_TICKS(ProjCfg::TimeoutMs::VoltagesQueueReceive));

            if (voltage_table == std::nullopt) {
                console.LogError("voltage table retrieval timeout!");
                return std::nullopt;
            }

            all_boards_voltages.push_back(*voltage_table);
        }

        return all_boards_voltages;
    }

    // helpers
    void PrintAllVoltagesFromTable(std::vector<PinsVoltages> const &voltages_tables)
    {
        for (auto const &table : voltages_tables) {
            console.Log("table for board: " + std::to_string(table.boardAddress));

            auto pin_counter = 0;
            for (auto pin_voltage : table.voltagesArray) {
                console.Log("   pin:" + std::to_string(pin_counter) + " v=" + std::to_string(pin_voltage));
                pin_counter++;
            }
        }
    }

    void StartVoltageMeasurementOnAllBoards()
    {
        for (auto const &semaphore : boardsSemaphores) {
            semaphore->Give();
        }
    }

    std::pair<UserCommand, std::vector<int>> WaitForUserCommand() noexcept
    {   // todo: make full parser
        while (true) {
            std::string cmd;
            cmd += *fromUserInputQ->Receive();

            for (;;) {
                auto result = fromUserInputQ->Receive(pdMS_TO_TICKS(10));
                if (result) {
                    cmd += (*result);
                }
                else
                    break;
            }

            auto words = StringParser::GetWords(cmd);

            console.Log("Command arrived: " + cmd);

            if (words.at(0) == "set") {
                int argument{ -1 };

                try {
                    argument = std::stoi(words.at(1));
                } catch (...) {
                    bluetooth->Write("invalid argument: " + words.at(1) + ". Example usage: set 14");
                }

                return { UserCommand::EnableOutputForPin, std::vector{ argument } };
            }
            else if (words.at(0) == "voltage") {
                if (words.at(1) == "high") {
                    SetOutputVoltageValue(OutputVoltageLevel::_09);
                }
                else if (words.at(1) == "low") {
                    SetOutputVoltageValue(OutputVoltageLevel ::_07);
                }
                else {
                    bluetooth->Write("invalid argument for command: " + words.at(0) + ". Respected arguments: low, high");
                }
            }
            else if (words.at(0) == "check") {
                if (words.size() == 1)
                    return { UserCommand::CheckConnections, std::vector<int>() };
                else if (words.size() == 3) {
                    int boardId = -1;

                    try {
                        boardId = std::stoi(words.at(1));
                    } catch (...) {
                        console.LogError("invalid command 'check', bad board argument");
                        continue;
                    }

                    int pinId = -1;
                    try {
                        pinId = std::stoi(words.at(2));
                    } catch (...) {
                        console.LogError("invalid command 'check' bad pin argument");
                        continue;
                    }

                    return { UserCommand::CheckConnections, std::vector{ boardId, pinId } };
                }
                else {
                    return { UserCommand::Unknown, std::vector<int>() };
                }
            }
            else if (words.at(0) == "getboards") {
                return { UserCommand::GetAllBoardsIds, std::vector{ 0 } };
            }

            return { UserCommand::Unknown, std::vector<int>() };
        }
    }

    // tests
    [[noreturn]] void UnitTestAllRead() noexcept
    {
        ioBoards.at(0)->SetVoltageAtPin(1);
        Task::DelayMs(500);
        while (true) {
            auto voltages = ioBoards.at(0)->GetAllPinsVoltages();
            if (voltages) {
                int pin_counter = 0;
                for (auto const voltage : *voltages) {
                    console.Log(std::to_string(pin_counter) + ' ' + std::to_string(voltage));
                    pin_counter++;
                }
            }

            Task::DelayMs(300);
        }
    }
    [[noreturn]] void UnitTestGetCounter() noexcept
    {
        while (true) {
            auto counter = ioBoards.at(0)->GetBoardCounterValue();

            if (counter) {
                console.Log("counter value = " + std::to_string(*counter));
            }

            Task::DelayMs(500);
        }
    }
    [[noreturn]] void UnitTestEnableOutputAtPin() noexcept
    {
        while (true) {
            for (auto pin_counter = 0; pin_counter < 32; pin_counter++) {
                ioBoards.at(0)->SetVoltageAtPin(pin_counter);
                Task::DelayMs(500);
            }
        }
    }
    [[noreturn]] void UnitTestGetVoltageFromPin() noexcept
    {
        while (true) {
            for (auto pin_counter = 0; pin_counter < 32; pin_counter++) {
                auto pin_v = ioBoards.at(0)->GetPinVoltage(pin_counter);

                if (pin_v) {
                    console.Log(std::to_string(pin_counter) + ' ' + std::to_string(*pin_v));
                }
                Task::DelayMs(500);
            }
        }
    }
    [[noreturn]] void UnitTestTwoCommands() noexcept
    {
        while (true) {
            auto voltages = ioBoards.at(0)->GetAllPinsVoltages();
            if (voltages) {
                int pin_counter = 0;
                for (auto const voltage : *voltages) {
                    console.Log(std::to_string(pin_counter) + ' ' + std::to_string(voltage));
                    pin_counter++;
                }
            }
            Task::DelayMs(500);
            for (auto pin_counter = 0; pin_counter < 32; pin_counter++) {
                auto pin_v = ioBoards.at(0)->GetPinVoltage(pin_counter);

                if (pin_v) {
                    console.Log(std::to_string(pin_counter) + ' ' + std::to_string(*pin_v));
                }
                Task::DelayMs(500);
            }
        }
    }
    [[noreturn]] void UnitTestCommunication(Byte board_addr) noexcept
    {
        ioBoards.emplace_back(std::make_shared<Board>(board_addr, pinsVoltagesResultsQ));
        auto constexpr test_data_len = 10;
        while (true) {
            auto data_to_slave = std::vector<Byte>(test_data_len);

            for (auto &value : data_to_slave) {
                value = rand() % 10 + 1;
            }

            ioBoards.at(0)->UnitTestCommunication(data_to_slave);
            //            ioBoards.at(0)->UnitTestCommunication(std::vector<Byte>{ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 });
            //                                                                     11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
            //                                                                     21, 22, 23, 24, 25, 26, 27, 28, 29, 30
            //                                                                     });

            Task::DelayMs(100);
        }
    }

    // tasks
    [[noreturn]] void CommandDirectorTask() noexcept
    {
        //                UnitTestAllRead();
        //        UnitTestGetCounter();
        //        UnitTestEnableOutputAtPin();
        //                UnitTestGetVoltageFromPin();
        //        UnitTestTwoCommands();
        //        UnitTestCommunication(0x25);
        while (true) {
            auto [command, args] = WaitForUserCommand();

            switch (command) {
            case UserCommand::CheckConnections: {
                if (args.size() == 0)
                    FindAllConnections();
                else {
                    FindConnectionsAtBoardForPin(args.at(0), args.at(1));
                }
            } break;
            case UserCommand::EnableOutputForPin: {
                EnableOutputForPin(args.at(0));
            } break;
            case UserCommand::GetAllBoardsIds: {
                SendAllBoardsIds();
            } break;
            default: break;
            }
        }
    }

  private:
    Apparatus(std::shared_ptr<Queue<char>> input_queue)
      : console{ "Main", ProjCfg::EnableLogForComponent::Main }
      , pinsVoltagesResultsQ{ std::make_shared<QueueT>(10) }
      , fromUserInputQ{ std::move(input_queue) }
      , bluetooth{ Bluetooth::Get() }
      , measurementsTask{ [this]() { CommandDirectorTask(); },
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

    SmartLogger          console;
    std::shared_ptr<IIC> i2c = nullptr;

    std::vector<std::shared_ptr<Board>>     ioBoards;
    std::vector<std::shared_ptr<Semaphore>> boardsSemaphores;
    std::shared_ptr<QueueT>                 pinsVoltagesResultsQ;

    std::shared_ptr<Queue<char>> fromUserInputQ;
    std::shared_ptr<Bluetooth>   bluetooth;
    Task                         measurementsTask;
};