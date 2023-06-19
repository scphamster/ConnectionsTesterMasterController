#pragma once
#include <memory>
#include <vector>

#include "board.hpp"
#include "data_link.hpp"
// #include "esp_logger.hpp"
#include "iic.hpp"
#include "task.hpp"
#include "queue.hpp"
#include "semaphore.hpp"
#include "bluetooth.hpp"
#include "string_parser.hpp"
#include "gpio.hpp"
#include "cmd_interpreter.hpp"
#include "communicator.hpp"
// #include "measurement_structures.hpp"
// #include "communicator_concept.hpp"

class Apparatus {
  public:
    using Byte               = uint8_t;
    using BoardAddrT         = Byte;
    using OneBoardVoltages   = Board::OneBoardVoltages;
    using OutputVoltageLevel = Board::OutputVoltage;
    using QueueT             = Queue<OneBoardVoltages>;
    using PinNumT            = Board::PinNumT;
    using CircuitParamT      = float;
    using VoltageT           = CircuitParamT;
    using ResistanceT        = CircuitParamT;
    using CommResult         = Board::Result;
    using CommandCatcher     = CommandInterpreter<Bluetooth>;
    using UserCommand        = typename CommandCatcher::UserCommand;
    using CommunicatorT      = Communicator<MessageToMaster>;

    void static Create(std::shared_ptr<CommunicatorT> socket) noexcept
    {
        _this = std::shared_ptr<Apparatus>{ new Apparatus{ std::move(socket) } };
    }
    auto static Get() noexcept
    {
        auto console = EspLogger::Get();

        if (_this == nullptr) {
            console->LogError("MainApparatus",
                              "Apparatus was not created yet, invoke Apparatus::Run before trying to Get instance");
            std::terminate();
        }

        return _this;
    }

    [[nodiscard]] bool                      BoardsSearchPerformed() const noexcept { return boardsSearchPerformed; }
    std::optional<std::vector<Board::Info>> GetBoards(bool perform_rescan = false) noexcept
    {
        if (perform_rescan) {
            FindAllConnectedBoards();
        }

        if (ioBoards.empty())
            return std::vector<Board::Info>();

        std::vector<Board::Info> v;
        v.reserve(ioBoards.size());

        for (const auto &board : ioBoards) {
            auto internals = board->GetInternalParameters(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);

            if (internals.second == std::nullopt) {
                console.LogError("Unsuccessful internal parameters retrieval for board " +
                                 std::to_string(board->GetAddress()));
                return std::nullopt;
            }

            if (internals.second->outputResistance1 == UINT16_MAX) {
                console.LogError("Board with address " + std::to_string(board->GetAddress()) +
                                 " has not set internal parameters!");

                internals.second->outputResistance1 = Board::GetInternalParametersCmd::STD_OUT_R;
                internals.second->outputResistance2 = Board::GetInternalParametersCmd::STD_OUT_R;
                internals.second->inputResistance1  = Board::GetInternalParametersCmd::STD_IN_R;
                internals.second->inputResistance2  = Board::GetInternalParametersCmd::STD_IN_R;
                internals.second->shuntResistance   = Board::GetInternalParametersCmd::STD_SHUNT_R;
                internals.second->outputVoltageLow  = Board::GetInternalParametersCmd::STD_LOW_OUT_V;
                internals.second->outputVoltageHigh = Board::GetInternalParametersCmd::STD_HIGH_OUT_V;
            }

            v.emplace_back(Board::Info{ *internals.second,
                                        board->GetAddress(),
                                        Board::GetFirmwareVersion::targetVersion,
                                        board->GetOutputVoltageLevel(),
                                        board->IsHealthy() });
        }

        return v;
    }
    std::optional<AllBoardsVoltages> MeasureAll() noexcept
    {
        auto voltages = GetAllVoltages(true);
        if (voltages == std::nullopt) {
            for (int retry_counter = 0; retry_counter < ProjCfg::FailHandle::GetAllVoltagesRetryTimes; retry_counter++) {
                voltages = GetAllVoltages(true);

                if (voltages != std::nullopt)
                    break;
            }
        }

        if (voltages == std::nullopt) {
            console.LogError("Get all voltages command failed with " +
                             std::to_string(ProjCfg::FailHandle::GetAllVoltagesRetryTimes) + " retry number");

            return std::nullopt;
        }

        return AllBoardsVoltages(std::move(*voltages));
    }
    void CheckAllConnections() noexcept { FindAndAnalyzeAllConnections(ConnectionAnalysis::Raw, true); }
    void CheckConnection(Board::PinAffinityAndId pin) noexcept
    {
        FindConnectionsForPinAtBoard(pin.pinId, pin.boardAddress, ConnectionAnalysis::Raw, true);
    }
    void EnableOutputForPin(BoardAddrT board_addr, PinNumT pin) noexcept
    {
        auto board = FindBoardWithAddress(board_addr);
        if (board == std::nullopt) {
            console.LogError("board with addr:" + std::to_string(board_addr) + " not found");
            return;
        }

        auto result = (*board)->SetVoltageAtPin(pin);
        if (result == CommResult::Good) {
            console.Log("Voltage at pin: " + std::to_string(pin) + " was set");
        }
        else {
            console.LogError("Voltage at pin: " + std::to_string(pin) + " failed to set");
        }
    }
    void EnableOutputForPin(Board::PinAffinityAndId pin_affinity_and_id) noexcept
    {
        EnableOutputForPin(pin_affinity_and_id.boardAddress, pin_affinity_and_id.pinId);
    }

    /**
     *
     * @param forcedDisable : true: sends to all available boards command to disable output despite state of output
     *                                  stored inside Board class
     */
    void DisableOutput(bool forcedDisable = false) const noexcept
    {
        for (const auto &board : ioBoards) {
            if (forcedDisable) {
                board->DisableOutput(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
            }
            else {
                if (board->OutputIsEnabled())
                    board->DisableOutput(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
            }
        }
    }

  protected:
    enum class ConnectionAnalysis {
        SimpleBoolean,
        Voltage,
        Resistance,
        Raw
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

        ioBoards.clear();

        Task::DelayMs(50);

        auto seq_mutex = std::make_shared<Mutex>();

        for (auto addr = start_address; addr <= last_address; addr++) {
            auto board_found = i2c->CheckIfSlaveWithAddressIsOnLine(addr);
            Task::DelayMs(5);

            if (board_found) {
                console.Log("board with address:" + std::to_string(addr) + " was found");
                ioBoards.emplace_back(std::make_shared<Board>(addr, pinsVoltagesResultsQ, seq_mutex));
                boardsSemaphores.push_back(ioBoards.back()->Get_MeasureAllTaskStart_Semaphore());
            }
        }

        if (ioBoards.size() == 0)
            console.LogError("No boards found, check was performed between addresses: " + std::to_string(start_address) +
                             " and " + std::to_string(last_address));

        Task::DelayMs(ProjCfg::BoardsConfigs::DelayBeforeCheckOfInternalCounterAfterInitializationMs);

        int board_counter = 0;
        for (auto const &board : ioBoards) {
            auto [comm_result, counter_value] =
              board->GetBoardCounterValue(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
            board->SetOutputVoltageValue(OutputVoltageLevel::_07, ProjCfg::FailHandle::CommandToBoardAttemptsNumber);

            auto result = board->CheckFWVersionCompliance(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
            if (result.first == CommResult::BadCommunication) {
                console.LogError("Board with address " + std::to_string(board->GetAddress()) +
                                 " has problems with communication!");
                ioBoards.erase(ioBoards.begin() + board_counter);
            }
            else if (result.first == CommResult::Good) {
                if (result.second == false) {
                    console.LogError("Board with address " + std::to_string(board->GetAddress()) +
                                     " has not compliant firmware version!");

                    ioBoards.erase(ioBoards.begin() + board_counter);
                }
            }
            else if (result.first == CommResult::BadAcknowledge) {
                console.LogError("Board with address " + std::to_string(board->GetAddress()) +
                                 " has no implemented GetFirmwareAddress command");
            }

            board_counter++;
        }

        boardsSearchPerformed = true;
    }
    void SendAllBoardsIds() noexcept
    {
        // todo: clenup this function
        return;

        std::string response = "HW dummyarg -> ";

        for (auto board : ioBoards) {
            response.append(std::to_string(board->GetAddress()) + ' ');
        }

        response.append("END\n");

        console.Log("response sent: " + response);
    }
    void SetOutputVoltageValue(OutputVoltageLevel level) noexcept
    {
        for (auto const &board : ioBoards) {
            console.Log("Setting voltage level");
            auto result = board->SetOutputVoltageValue(level, ProjCfg::FailHandle::CommandToBoardAttemptsNumber);

            if (result != CommResult::Good) {
                Task::DelayMs(100);
                board->SetOutputVoltageValue(level, ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
            }

            Task::DelayMs(10);
        }
    }

    bool FindConnectionsForPinAtBoard(PinNumT                pin,
                                      std::shared_ptr<Board> board,
                                      ConnectionAnalysis     analysis_type,
                                      bool                   sequential)
    {
        auto result = board->SetVoltageAtPin(pin, ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
        if (result != CommResult::Good) {
            console.LogError("Setting pin voltage unsuccessful");
            return false;
        }

        Task::DelayMs(ProjCfg::BoardsConfigs::DelayAfterPinVoltageSetMs);

        auto voltage_tables_from_all_boards = GetAllVoltages(sequential);
        if (board->DisableOutput(ProjCfg::BoardsConfigs::DisableOutputRetryTimes) != CommResult::Good) {
            console.LogError("Disable output unsuccessful");
            return false;
        }

        if (voltage_tables_from_all_boards == std::nullopt) {
            console.LogError("Voltage tables not obtained!");
            return false;
        }

        std::string response_header;
        switch (analysis_type) {
        case ConnectionAnalysis::SimpleBoolean: response_header = "CONNECT"; break;
        case ConnectionAnalysis::Voltage: response_header = "VOLTAGES"; break;
        case ConnectionAnalysis::Resistance: response_header = "RESISTANCES"; break;
        case ConnectionAnalysis::Raw: response_header = "CONN_RAW"; break;
        default: {
            console.LogError("Bad analysis type for find connections command: " +
                             std::to_string(static_cast<int>(analysis_type)));
            std::terminate();
        }
        }

        std::string answer_to_master = response_header + ' ' + std::to_string(board->GetAddress()) + ':' +
                                       std::to_string(Board::GetHarnessPinNumFromLogicPinNum(pin)) + " -> ";

        using ConnectionData = PinConnectivity::PinConnectionData;
        using PinDescriptor  = PinConnectivity::PinAffinityAndId;

        auto master_pin =
          PinDescriptor{ board->GetAddress(), static_cast<Byte>(Board::GetHarnessPinNumFromLogicPinNum(pin)) };
        std::vector<ConnectionData> connections{};

        for (const auto &voltage_table_from_board : *voltage_tables_from_all_boards) {
            std::string board_affinity = std::to_string(voltage_table_from_board.boardAddress);

            auto pin_counter = 0;
            for (auto voltage : voltage_table_from_board.pinsVoltages) {
                auto harness_pin_id = Board::GetHarnessPinNumFromLogicPinNum(pin_counter);

                if (pin == pin_counter and board->GetAddress() == voltage_table_from_board.boardAddress) {
                    if (voltage == 0) {
                        console.LogError("Pin is not connected to itself!");
                        return false;
                    }
                }

                if (voltage > 0) {
                    if (analysis_type == ConnectionAnalysis::SimpleBoolean) {
                        answer_to_master.append(board_affinity + ':' + std::to_string(harness_pin_id) + ' ');
                    }
                    else if (analysis_type == ConnectionAnalysis::Resistance) {
                        answer_to_master.append(board_affinity + ':' + std::to_string(harness_pin_id) + '(' +
                                                StringParser::ConvertFpValueWithPrecision(
                                                  board->CalculateConnectionResistanceFromAdcValue(voltage),
                                                  1) +
                                                ") ");
                    }
                    else if (analysis_type == ConnectionAnalysis::Voltage) {
                        answer_to_master.append(board_affinity + ':' + std::to_string(harness_pin_id) + '(' +
                                                StringParser::ConvertFpValueWithPrecision(voltage, 2) + ") ");
                    }
                    else if (analysis_type == ConnectionAnalysis::Raw) {
                        answer_to_master.append(board_affinity + ':' + std::to_string(harness_pin_id) + '(' +
                                                std::to_string(voltage) + ") ");
                    }

                    connections.emplace_back(ConnectionData{
                      PinDescriptor{ voltage_table_from_board.boardAddress, static_cast<Byte>(harness_pin_id) },
                      voltage });
                }

                pin_counter++;
            }
        }

        answer_to_master.append("END\n");

        console.Log(answer_to_master);

        if (not socket->GetToMasterSB()->Send(PinConnectivity(master_pin, std::move(connections)).Serialize())) {
            console.LogError("Unsuccessful send to streambuffer! Pin: " + std::to_string(board->GetAddress()) + ":" +
                             std::to_string(pin));
        }
        //        Task::DelayMs(3);

        return true;
    }
    void FindConnectionsForPinAtBoard(PinNumT            pin,
                                      BoardAddrT         board_address,
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
            board->DisableOutput(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
        }

        for (auto board : ioBoards) {
            FindAndAnalyzeAllConnectionsForBoard(board, analysis_type, sequential);
        }
    }
    void GetBoardCounter(BoardAddrT board_addr)
    {
        auto board = FindBoardWithAddress(board_addr);
        if (not board) {
            console.LogError("Board not found : " + std::to_string(board_addr));
            return;
        }

        auto [comm_result, counter_value] =
          (*board)->GetBoardCounterValue(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
        std::string response_string;

        if (comm_result == CommResult::Good) {
            response_string =
              "TIMER " + std::to_string(board_addr) + " dummyarg -> " + std::to_string(*counter_value) + " END\n";
        }

        console.Log(response_string);
    }
    std::optional<std::vector<OneBoardVoltages>> GetAllVoltages(bool sequential) noexcept
    {
        pinsVoltagesResultsQ->Flush();
        StartVoltageMeasurementOnAllBoards(sequential);

        std::vector<OneBoardVoltages> all_boards_voltages;

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

    void GetInternalParametersForBoard(BoardAddrT board_addr) noexcept
    {
        auto board = FindBoardWithAddress(board_addr);
        if (!board) {
            console.LogError("requested board with addr:" + std::to_string(board_addr) + " does not exist!");
            return;
        }

        auto result = (*board)->GetInternalParameters(ProjCfg::FailHandle::CommandToBoardAttemptsNumber);
        if (result.first != CommResult::Good)
            return;

        std::string answer = "INTERNALS " + std::to_string(board_addr) + " -> ";
        answer.append(std::to_string(result.second->inputResistance1) + ' ');
        answer.append(std::to_string(result.second->outputResistance1) + ' ');
        answer.append(std::to_string(result.second->inputResistance2) + ' ');
        answer.append(std::to_string(result.second->outputResistance2) + ' ');
        answer.append(std::to_string(result.second->shuntResistance) + ' ');
        answer.append(std::to_string(result.second->outputVoltageLow) + ' ');
        answer.append(std::to_string(result.second->outputVoltageHigh) + " END\n");
        console.Log(answer);
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
    void StartVoltageMeasurementOnAllBoards(bool sequential)
    {
        for (auto const &semaphore : boardsSemaphores) {
            semaphore->Give();
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
    void PrintAllVoltagesFromTable(std::vector<OneBoardVoltages> const &voltages_tables) noexcept
    {
        for (auto const &table : voltages_tables) {
            console.Log("table for board: " + std::to_string(table.boardAddress));

            auto pin_counter = 0;
            for (auto pin_voltage : table.pinsVoltages) {
                console.Log("   pin:" + std::to_string(pin_counter) + " v=" + std::to_string(pin_voltage));
                pin_counter++;
            }
        }
    }

  private:
    Apparatus(std::shared_ptr<CommunicatorT> new_socket)
      : console{ "Main", ProjCfg::EnableLogForComponent::Main }
      , pinsVoltagesResultsQ{ std::make_shared<QueueT>(10) }
      , socket{ std::move(new_socket) }
    {
        IIC::Create(IIC::Role::Master,
                    ProjCfg::BoardsConfigs::SDA_Pin,
                    ProjCfg::BoardsConfigs::SCL_Pin,
                    ProjCfg::BoardsConfigs::IICSpeedHz);
        i2c = IIC::Get();
        Init();
    }

    std::shared_ptr<Apparatus> static _this;

    Logger               console;
    std::shared_ptr<IIC> i2c = nullptr;

    std::vector<std::shared_ptr<Board>>     ioBoards;
    std::vector<std::shared_ptr<Semaphore>> boardsSemaphores;
    std::shared_ptr<QueueT>                 pinsVoltagesResultsQ;

    std::shared_ptr<CommunicatorT> socket;

    bool boardsSearchPerformed{ false };
};