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
#include "gpio.hpp"

class Apparatus {
  public:
    using Byte               = uint8_t;
    using BoardAddrT         = Byte;
    using PinsVoltages       = Board::PinsVoltages;
    using OutputVoltageLevel = Board::OutputVoltage;
    using QueueT             = Queue<PinsVoltages>;
    using PinNumT            = Board::PinNumT;
    using CircuitParamT      = float;
    using VoltageT           = CircuitParamT;
    using ResistanceT        = CircuitParamT;
    using CommResult         = Board::Result;

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
        CheckResistances,
        CheckVoltages,
        GetAllBoardsIds,
        GetInternalCounter,
        GetTaskStackWatermark,
        SetNewAddressForBoard,
        Test,
        Unknown
    };
    enum class ConnectionAnalysis {
        SimpleBoolean,
        Voltage,
        Resistance
    };
    struct SetPinVoltageCmd {
        enum SpecialPinConfigurations : Byte {
            DisableAll = 254
        };
    };
    struct PinOnBoard {
        int boardAffinity = -1;
        int idxOnBoard    = -1;
    };

    void Init() noexcept { FindAllConnectedBoards(); }

    void FindAllConnectedBoards() noexcept
    {
        auto constexpr start_address = 0x01;
        auto constexpr last_address  = 0x7F;

        Task::DelayMs(500);

        for (auto addr = start_address; addr <= last_address; addr++) {
            auto board_found = i2c->CheckIfSlaveWithAddressIsOnLine(addr);
            Task::DelayMs(5);

            if (board_found) {
                console.Log("board with address:" + std::to_string(addr) + " was found");
                ioBoards.emplace_back(std::make_shared<Board>(addr, pinsVoltagesResultsQ));
                boardsSemaphores.push_back(ioBoards.back()->GetStartSemaphore());
            }
        }

        if (ioBoards.size() == 0)
            console.LogError("No boards found, check was performed between addresses: " + std::to_string(start_address) +
                             " and " + std::to_string(last_address));

        Task::DelayMs(ProjCfg::BoardsConfigs::DelayBeforeCheckOfInternalCounterAfterInitializationMs);

        for (auto const &board : ioBoards) {
            auto [comm_result, counter_value] =
              board->GetBoardCounterValue(ProjCfg::BoardsConfigs::CommandSendRetryNumber);
            board->SetOutputVoltageValue(OutputVoltageLevel::_07, ProjCfg::BoardsConfigs::CommandSendRetryNumber);
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
            console.Log("Setting voltage level");
            board->SetOutputVoltageValue(level, ProjCfg::BoardsConfigs::CommandSendRetryNumber);
            Task::DelayMs(5);
        }
    }

    bool FindConnectionsForPinAtBoard(PinNumT                pin,
                                      std::shared_ptr<Board> board,
                                      ConnectionAnalysis     analysis_type,
                                      bool                   sequential)
    {
        auto result = board->SetVoltageAtPin(pin, ProjCfg::BoardsConfigs::CommandSendRetryNumber);
        if (result != CommResult::Good)
            return false;

        Task::DelayMs(ProjCfg::BoardsConfigs::DelayAfterPinVoltageSetMs);

        auto voltage_tables_from_all_boards = MeasureAll(sequential);
        board->DisableOutput();

        if (voltage_tables_from_all_boards == std::nullopt) {
            console.LogError("Unsuccessful!");
            return false;
        }

        std::string response_header;
        switch (analysis_type) {
        case ConnectionAnalysis::SimpleBoolean: {
            response_header = "CONNECT";
        } break;
        case ConnectionAnalysis::Voltage: {
            response_header = "VOLTAGES";
        } break;
        case ConnectionAnalysis::Resistance: {
            response_header = "RESISTANCES";
        } break;
        default: {
            console.LogError("Bad analysis type for find connections command: " +
                             std::to_string(static_cast<int>(analysis_type)));
            std::terminate();
        }
        }

        std::string answer_to_master =
          response_header + ' ' + std::to_string(board->GetAddress()) + ':' + std::to_string(pin) + " -> ";

        for (const auto &voltage_table_from_board : *voltage_tables_from_all_boards) {
            std::string board_affinity = std::to_string(voltage_table_from_board.boardAddress);

            auto pin_counter = 0;
            for (auto voltage : voltage_table_from_board.voltagesArray) {
                if (pin == pin_counter and board->GetAddress() == voltage_table_from_board.boardAddress) {
                    if (voltage == 0) {
                        console.LogError("Pin is not connected to itself!");
                        return false;
                    }
                }

                if (voltage > 0) {
                    if (analysis_type == ConnectionAnalysis::SimpleBoolean) {
                        answer_to_master.append(board_affinity + ':' + std::to_string(pin_counter) + ' ');
                    }
                    else if (analysis_type == ConnectionAnalysis::Resistance) {
                        answer_to_master.append(
                          board_affinity + ':' + std::to_string(pin_counter) + '(' +
                          StringParser::ConvertFpValueWithPrecision(CalculateConnectionResistanceFromAdcValue(voltage),
                                                                    1) +
                          ") ");
                    }
                    else if (analysis_type == ConnectionAnalysis::Voltage) {
                        answer_to_master.append(
                          board_affinity + ':' + std::to_string(pin_counter) + '(' +
                          StringParser::ConvertFpValueWithPrecision((voltage), 2) + ") ");
                    }
                }

                pin_counter++;
            }
        }

        answer_to_master.append("END\n");

        console.Log(answer_to_master);
        bluetooth->Write(answer_to_master);

        //        Task::DelayMs(3);

        return true;
    }
    void FindAndAnalyzeAllConnectionsForBoard(std::shared_ptr<Board> board,
                                              ConnectionAnalysis     analysis_type,
                                              bool                   sequential)
    {
        constexpr auto pin_count_at_board = Board::pinCount;
        int            retry_count        = ProjCfg::BoardsConfigs::PinConnectionsCheckRetryCount;

        std::vector<PinNumT> failedPins;

        for (PinNumT pin = 0; pin < pin_count_at_board; pin++) {
            if (not FindConnectionsForPinAtBoard(pin, board, analysis_type, sequential)) {
                failedPins.emplace_back(pin);
            }
        }

        if (failedPins.empty())
            return;

        do {
            std::vector<PinNumT> failedPinsSecondTime;

            for (auto const pin : failedPins) {
                if (not FindConnectionsForPinAtBoard(pin, board, analysis_type, sequential)) {
                    failedPinsSecondTime.emplace_back(pin);
                }
            }

            failedPins = std::move(failedPinsSecondTime);

            retry_count--;
        } while (retry_count > 0);

        return;
    }
    void FindAndAnalyzeAllConnections(ConnectionAnalysis analysis_type, bool sequential) noexcept
    {
        console.Log("Executing command: FindAndAnalyzeAllConnections");

        for (auto board : ioBoards) {
            board->DisableOutput(ProjCfg::BoardsConfigs::CommandSendRetryNumber);
        }

        for (auto board : ioBoards) {
            FindAndAnalyzeAllConnectionsForBoard(board, analysis_type, sequential);
        }
    }
    void FindConnectionsAtBoardForPin(BoardAddrT         board_address,
                                      PinNumT            pin,
                                      ConnectionAnalysis analysis_type,
                                      bool               sequential)
    {
        if (pin > Board::pinCount) {
            console.LogError("requested pin number is higher than pin count at one board, requested pin: " +
                             std::to_string(pin));
            return;
        }

        auto board = FindBoardWithAddress(board_address);
        if (not board) {
            console.LogError("Board with address: " + std::to_string(board_address) + " not found");
            return;
        }

        FindConnectionsForPinAtBoard(pin, *board, analysis_type, sequential);
    }
    // todo change name
    void GetBoardCounter(BoardAddrT board_addr)
    {
        auto board = FindBoardWithAddress(board_addr);
        if (not board) {
            console.LogError("Board not found : " + std::to_string(board_addr));
            return;
        }

        auto [comm_result, counter_value] =
          (*board)->GetBoardCounterValue(ProjCfg::BoardsConfigs::CommandSendRetryNumber);
        std::string response_string;

        if (comm_result == CommResult::Good) {
            response_string =
              "TIMER " + std::to_string(board_addr) + " dummyarg -> " + std::to_string(*counter_value) + " END\n";
        }

        bluetooth->Write(response_string);
        console.Log(response_string);
    }
    std::optional<std::vector<PinsVoltages>> MeasureAll(bool sequential) noexcept
    {
        pinsVoltagesResultsQ->Flush();
        StartVoltageMeasurementOnAllBoards(sequential);

        std::vector<PinsVoltages> all_boards_voltages;

        for (auto board = 0; board < ioBoards.size(); board++) {
            auto voltage_table = pinsVoltagesResultsQ->Receive(pdMS_TO_TICKS(ProjCfg::TimeoutMs::VoltagesQueueReceive));

            if (voltage_table == std::nullopt) {
                console.LogError("voltage table retrieval timeout!");
                return std::nullopt;
            }

            if (voltage_table->readResult != CommResult::Good) {
                console.LogError("Bad measure all result");
                return std::nullopt;
            }

            all_boards_voltages.push_back(*voltage_table);
        }

        return all_boards_voltages;
    }

    // helpers
    std::optional<std::shared_ptr<Board>> FindBoardWithAddress(BoardAddrT board_address) noexcept
    {
        auto board_it = std::find_if(ioBoards.begin(), ioBoards.end(), [board_address](auto board) {
            return board->GetAddress() == board_address;
        });

        if (board_it == ioBoards.end()) {
            //            console.LogError("No board found with address: " + std::to_string(board_address));
            return std::nullopt;
        }

        return *board_it;
    }
    ResistanceT CalculateConnectionResistanceFromAdcValue(Board::ADCValueT adc_value)
    {
        CircuitParamT constexpr output_resistance = 200;
        CircuitParamT constexpr input_resistance  = 1200;
        CircuitParamT constexpr shunt_resistance  = 330;
        CircuitParamT constexpr output_voltage    = 0.7f;
        CircuitParamT constexpr calibration_value = 120.f;

        // todo: shrink to single line
        auto voltage            = CalculateVoltageFromAdcValue(adc_value);
        auto circuit_current    = voltage / shunt_resistance;
        auto overall_resistance = output_voltage / circuit_current;
        auto test_resistance =
          overall_resistance - output_resistance - input_resistance - shunt_resistance + calibration_value;
        return test_resistance;
    }
    VoltageT CalculateVoltageFromAdcValue(Board::ADCValueT adc_value)
    {
        VoltageT constexpr reference = 1.1;

        return (static_cast<VoltageT>(adc_value) / 1024) * reference;
    }
    void StartVoltageMeasurementOnAllBoards(bool sequential)
    {
        for (auto const &semaphore : boardsSemaphores) {
            semaphore->Give();
            if (sequential) {
                Task::DelayMs(Board::VoltageCheckCmd::timeToWaitForResponseAllPinsMs);
            }
        }
    }

    std::pair<UserCommand, std::vector<int>> WaitForUserCommand() noexcept
    {
        while (true) {
            std::string cmd;
            cmd += *fromUserInputQ->Receive();

            // make sure whole command arrived, not sure if this needed
            for (;;) {
                auto result = fromUserInputQ->Receive(pdMS_TO_TICKS(10));
                if (result) {
                    cmd += (*result);
                }
                else
                    break;
            }
            console.Log("Command arrived: " + cmd);

            auto words = StringParser::GetWords(cmd);
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
                if (words.size() >= 2) {
                    auto second_word = words.at(1);

                    auto user_command = UserCommand();

                    if (second_word == "connections")
                        user_command = UserCommand::CheckConnections;
                    else if (second_word == "resistances")
                        user_command = UserCommand::CheckResistances;
                    else if (second_word == "voltages")
                        user_command = UserCommand::CheckVoltages;
                    else {
                        console.LogError("Unknown command: " + second_word);
                        return { UserCommand::Unknown, std::vector<int>() };
                    }

                    if (words.size() == 2) {
                        return { user_command, std::vector<int>() };
                    }

                    if (words.size() == 3) {
                        if (words.at(2) == "sequential") {
                            return { user_command, std::vector<int>{ 1 } };
                        }
                        else {
                            return { UserCommand::Unknown, std::vector<int>() };
                        }
                    }

                    if (words.size() != 4) {
                        console.LogError("Bad 'check' command -> number of arguments is not 2 nor 4");
                        return { UserCommand::Unknown, std::vector<int>() };
                    }

                    int board_argument;
                    try {
                        board_argument = std::stoi(words.at(2));
                    } catch (...) {
                        console.LogError("Bad board argument for command 'check': " + words.at(2));
                        return { UserCommand::Unknown, std::vector<int>() };
                    }

                    int pin_argument;
                    try {
                        pin_argument = std::stoi(words.at(3));
                    } catch (...) {
                        console.LogError("Bad pin argument for command 'check': " + words.at(3));
                        return { UserCommand::Unknown, std::vector<int>() };
                    }

                    return { user_command, std::vector(board_argument, pin_argument) };
                }
                else if (words.size() == 4) {
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
            else if (words.at(0) == "counter") {
                if (words.size() == 0) {
                    console.LogError("No board address argument provided for command: 'counter'");
                    continue;
                }

                int board_addr = -1;
                try {
                    board_addr = std::stoi(words.at(1));
                } catch (...) {
                    console.LogError("bad argument for command: 'counter' : " + words.at(1));
                    continue;
                }

                return { UserCommand::GetInternalCounter, std::vector{ board_addr } };
            }
            else if (words.at(0) == "stack") {
                return { UserCommand::GetTaskStackWatermark, std::vector<int>() };
            }
            else if (words.at(0) == "newaddress") {
                int new_addr = -1;
                try {
                    new_addr = std::stoi(words.at(1));
                } catch (...) {
                    bluetooth->Write("bad argument for command\n");
                    console.LogError("wrong argument for commmand");
                    return { UserCommand::Unknown, std::vector<int>() };
                }

                auto user_command = UserCommand::SetNewAddressForBoard;

                if (words.size() == 2)
                    return { user_command, std::vector{ new_addr } };

                int current_address = new_addr;

                try {
                    new_addr = std::stoi(words.at(2));
                } catch (...) {
                    bluetooth->Write("bad argument for command\n");
                    console.LogError("bad argument for command");
                    return { UserCommand::Unknown, std::vector<int>() };
                }

                return { user_command, std::vector{ current_address, new_addr } };
            }
            else if (words.at(0) == "test") {
                return { UserCommand::Test, std::vector<int>() };
            }

            return { UserCommand::Unknown, std::vector<int>() };
        }
    }

    // tests
    [[noreturn]] void UnitTestAllRead() noexcept
    {
        Task::DelayMs(500);
        int pin         = 0;
        int bad_pin     = -1;
        int bad_voltage = -1;
        while (true) {
            ioBoards.at(0)->SetVoltageAtPin(pin);
            Task::DelayMs(3);
            auto [comm_result, voltages] = ioBoards.at(0)->GetAllPinsVoltages();

            if (comm_result != CommResult::Good) {
                console.LogError("Bad result from getting all voltages!");
                continue;
            }

            pin++;
            Task::DelayMs(5);
            if (pin == 31)
                pin = 0;
        }
    }
    [[noreturn]] void UnitTestGetCounter() noexcept
    {
        while (true) {
            auto [comm_result, counter] = ioBoards.at(0)->GetBoardCounterValue();

            if (comm_result != CommResult::Good) {
                console.LogError("Bad!");
                continue;
            }

            console.Log("counter value = " + std::to_string(*counter));

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
                auto [comm_result, pin_v] = ioBoards.at(0)->GetPinVoltage(pin_counter);

                if (comm_result != CommResult::Good) {
                    console.LogError("Bad");
                    continue;
                }

                console.Log(std::to_string(pin_counter) + ' ' + std::to_string(*pin_v));
                Task::DelayMs(500);
            }
        }
    }
    [[noreturn]] void UnitTestTwoCommands() noexcept
    {
        while (true) {
            auto [comm_result, voltages] = ioBoards.at(0)->GetAllPinsVoltages();

            if (comm_result != CommResult::Good) {
                console.LogError("Get All bad result!");
                continue;
            }

            int pin_counter = 0;
            for (auto const voltage : *voltages) {
                console.Log(std::to_string(pin_counter) + ' ' + std::to_string(voltage));
                pin_counter++;
            }

            Task::DelayMs(500);
            for (auto pin_counter = 0; pin_counter < 32; pin_counter++) {
                auto [comm_result, pin_v] = ioBoards.at(0)->GetPinVoltage(pin_counter);

                if (comm_result != CommResult::Good) {
                    console.LogError("Bad response at pin:" + std::to_string(pin_counter));
                    break;
                }

                console.Log(std::to_string(pin_counter) + ' ' + std::to_string(*pin_v));
                Task::DelayMs(500);
            }
        }
    }
    [[noreturn]] void UnitTestCommunication() noexcept
    {
        auto constexpr test_data_len = 10;

        if (!ioBoards.at(0)->StartTest())
            std::terminate();

        auto board_addr = ioBoards.at(0)->GetAddress();

        Task::DelayMs(200);

        while (true) {
            auto data = std::array<uint16_t, 32>();
            auto res  = i2c->Write(board_addr, std::vector<Byte>{ 150 }, 200);

            if (res != IIC::OperationResult::OK) {
                console.LogError("Bad write");
                continue;
            }

            Task::DelayMs(100);
            auto [operation_result, result] = i2c->Read<decltype(data)>(ioBoards.at(0)->GetAddress(), 200);

            if (operation_result != IIC::OperationResult::OK) {
                console.LogError("Unsuccessful read!");
                continue;
            }

            int counter = 0;
            for (auto const &value : *result) {
                console.Log(std::to_string(value));
                console.LogError("Counter= " + std::to_string(counter) + " value:" + std::to_string(value));
            }

            Task::DelayMs(100);
        }
    }
    void PrintAllVoltagesFromTable(std::vector<PinsVoltages> const &voltages_tables) noexcept
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

    // tasks
    [[noreturn]] void CommandDirectorTask() noexcept
    {
        while (true) {
            auto [command, args] = WaitForUserCommand();

            switch (command) {
            case UserCommand::CheckConnections: {
                if (args.size() == 0)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::SimpleBoolean, false);
                else if (args.size() == 1)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::SimpleBoolean, true);
                else {
                    FindConnectionsAtBoardForPin(args.at(0), args.at(1), ConnectionAnalysis::SimpleBoolean, true);
                }
            } break;
            case UserCommand::CheckVoltages: {
                if (args.size() == 0)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::Voltage, false);
                else if (args.size() == 1)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::Voltage, true);
                else {
                    FindConnectionsAtBoardForPin(args.at(0), args.at(1), ConnectionAnalysis::Voltage, true);
                }
            } break;
            case UserCommand::CheckResistances: {
                if (args.size() == 0)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::Resistance, false);
                else if (args.size() == 1)
                    FindAndAnalyzeAllConnections(ConnectionAnalysis::Resistance, true);
                else {
                    FindConnectionsAtBoardForPin(args.at(0), args.at(1), ConnectionAnalysis::Resistance, true);
                }
            } break;
            case UserCommand::EnableOutputForPin: {
                EnableOutputForPin(args.at(0));
            } break;
            case UserCommand::GetAllBoardsIds: {
                SendAllBoardsIds();
            } break;
            case UserCommand::GetInternalCounter: {
                GetBoardCounter(args.at(0));
            } break;
            case UserCommand::GetTaskStackWatermark: {
                auto watermark = Task::GetStackWatermarkOfThisTask();
                auto response  = "STACK dummyarg -> " + std::to_string(watermark) + " END\n";
                bluetooth->Write(response);
                console.Log(response);
            } break;
            case UserCommand::SetNewAddressForBoard:
                if (args.size() == 1) {
                    if (ioBoards.size() != 1) {
                        bluetooth->Write("No, or more than one boards are connected, specify current address with first "
                                         "argument: newaddress [current address] [new address]\n");
                        console.LogError("No, or more than one boards are connected, specify current address with first "
                                         "argument: newaddress [current address] [new address]\n");
                    }

                    auto result = ioBoards.at(0)->SetNewBoardAddress(args.at(0));

                    if (result == CommResult::Good) {
                        bluetooth->Write("CMD OK END\n");
                    }
                    else {
                        bluetooth->Write("CMD FAIL END\n");
                    }
                }
                else {
                    auto board = FindBoardWithAddress(args.at(0));
                    if (board) {
                        auto result = (*board)->SetNewBoardAddress(args.at(1));
                        if (result == CommResult::Good) {
                            bluetooth->Write("CMD OK END\n");
                        }
                        else {
                            bluetooth->Write("CMD FAIL END\n");
                        }
                    }
                    else {
                        bluetooth->Write("Board not found");
                        console.LogError("Board not found");
                    }
                }
                break;

            case UserCommand::Test: UnitTestAllRead(); break;

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
        testPin.SetLevel(Pin::Level::Low);
        Init();

        measurementsTask.Start();
    }

    std::shared_ptr<Apparatus> static _this;

    SmartLogger          console;
    std::shared_ptr<IIC> i2c = nullptr;

    std::vector<std::shared_ptr<Board>>     ioBoards;
    std::vector<std::shared_ptr<Semaphore>> boardsSemaphores;
    std::shared_ptr<QueueT>                 pinsVoltagesResultsQ;

    Pin testPin{ 26, Pin::Direction::Output };

    std::shared_ptr<Queue<char>> fromUserInputQ;
    std::shared_ptr<Bluetooth>   bluetooth;
    Task                         measurementsTask;
};