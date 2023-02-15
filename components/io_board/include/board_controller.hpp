#pragma once
#include <cstdlib>
#include <optional>
#include <mutex>

#include "data_link.hpp"

#include "queue.hpp"
#include "task.hpp"
#include "semaphore.hpp"
#include "utilities.hpp"
#include "my_mutex.hpp"
// todo: implement internal timer to not bother user about delays
class Board {
  public:
    auto static constexpr pinCount = ProjCfg::BoardsConfigs::NumberOfPins;
    using Byte                     = uint8_t;
    using PinNumT                  = size_t;
    using AddressT                 = Byte;
    using FirmwareVersionT         = Byte;
    using ADCValueT                = uint16_t;
    using AdcValueLoRes            = Byte;
    using InternalCounterT         = uint32_t;
    using CommandArgT              = Byte;
    using AllPinsVoltages          = std::array<ADCValueT, pinCount>;
    using AllPinsVoltages8B        = std::array<AdcValueLoRes, pinCount>;
    using OutputVoltageRealT       = float;
    using CircuitParamT            = float;
    using VoltageT                 = CircuitParamT;
    using ResistanceT              = CircuitParamT;
    auto static constexpr logicPinToPinOnBoardMapping =
      std::array<PinNumT, pinCount>{ 3,  28, 2,  29, 1, 30, 0, 31, 27, 4,  26, 5,  25, 6,  24, 7,
                                     11, 20, 10, 21, 9, 22, 8, 23, 19, 12, 18, 13, 17, 14, 16, 15 };

    auto static constexpr harnessToLogicPinNumMapping =
      std::array<PinNumT, pinCount>{ 6,  4,  2,  0,  9,  11, 13, 15, 22, 20, 18, 16, 25, 27, 29, 31,
                                     30, 28, 26, 24, 17, 19, 21, 23, 14, 12, 10, 8,  1,  3,  5,  7 };

    enum class Result {
        BadCommunication,
        BadAcknowledge,
        BadAnswerFormat,
        UnhealthyAnswerValue,
        Good,
        BoardAnsweredFail
    };

    struct OneBoardVoltages {
        Result            readResult;
        AddressT          boardAddress;
        AllPinsVoltages8B voltagesArray;
    };
    using VoltagesQ = Queue<OneBoardVoltages>;
    enum class Command {
        SetPinVoltage      = 0xC1,
        GetInternalCounter = 0xC2,
        GetPinVoltage      = 0xC3,
        SetOutputVoltage   = 0xc4,
        StartTest          = 0xc6,
    };
    enum BoardAnswer : Byte {
        OK                    = 0xa5,
        REPEAT_CMD_TO_CONFIRM = 0xAF,
        FAIL                  = 0x50
    };
    enum class OutputVoltage : uint8_t {
        Undefined = 0,
        _09       = ProjCfg::high_voltage_reference_select_pin,
        _07       = ProjCfg::low_voltage_reference_select_pin,
    };

    struct VoltageCheckCmd {
        using PinNum = Byte;
        enum SpecialMeasurements : Byte {
            MeasureAll = 33,
            MeasureVCC,
            MeasureGND
        };

        TickType_t static constexpr timeToWaitForResponseAllPinsMs =
          ProjCfg::BoardsConfigs::DelayBeforeReadAllPinsVoltagesResult;
        TickType_t static constexpr timeToWaitForResponseOnePinMs = 15;
        int static constexpr delayForSequentialRun                = timeToWaitForResponseAllPinsMs + 3;
        PinNum pin;
        Byte static constexpr rawHealthyVoltageValueIsBelow = 220;
    };
    struct VoltageSetCmd {
        enum class Special {
            DisableAll = 254
        };
    };
    struct SetNewBoardAddressCmd {
        auto static constexpr confirmationStagesNum = 6;
        using CommandArgumentsT                     = std::array<Byte, confirmationStagesNum>;

        Byte static constexpr command                    = 0xc5;
        size_t static constexpr delayBeforeResultCheckMs = 150;

        auto constexpr static NUMBER_OF_CONFIRMATIONS_BEFORE_EXECUTION = 3;

        constexpr SetNewBoardAddressCmd(Byte new_address) noexcept
          : newAddress{ new_address }
          , commandArguments{ new_address, new_address, firstPassword, secondPassword, firstPassword, new_address }
        { }

        Byte GetArgumentForStage(int stage_number) noexcept
        {
            if (stage_number > confirmationStagesNum)
                std::terminate();

            return commandArguments.at(stage_number);
        }
        CommandArgumentsT                       GetAllArguments() noexcept { return commandArguments; }
        Byte                                    newAddress     = 0;
        Byte                                    firstPassword  = 153;
        Byte                                    secondPassword = 102;
        std::array<Byte, confirmationStagesNum> commandArguments;
    };
    struct GetInternalCounter {
        Byte static constexpr command                  = ToUnderlying(Command::GetInternalCounter);
        size_t static constexpr delayBeforeResultCheck = 100;
    };
    struct GetFirmwareVersion {
        Byte static constexpr cmd                = 0xc7;
        Byte static constexpr targetVersion      = 19;
        auto static constexpr delayForResponseMs = 2;
    };
    struct SetInternalParametersCmd {
        Byte static constexpr cmd                = 0xC8;
        auto static constexpr numberOfParams     = 7;
        auto static constexpr delayForResponseMs = 100;
        using InternalParamT                     = uint16_t;
        using InternalParamsT                    = std::array<InternalParamT, numberOfParams>;
    };
    struct GetInternalParametersCmd {
        Byte static constexpr cmd                = 0xc9;
        auto static constexpr delayForResponseMs = 200;

        struct InternalParamsT {
            SetInternalParametersCmd::InternalParamT outputResistance1;
            SetInternalParametersCmd::InternalParamT inputResistance1;
            SetInternalParametersCmd::InternalParamT outputResistance2;
            SetInternalParametersCmd::InternalParamT inputResistance2;
            SetInternalParametersCmd::InternalParamT shuntResistance;
            SetInternalParametersCmd::InternalParamT outputVoltageLow;
            SetInternalParametersCmd::InternalParamT outputVoltageHigh;
        };
    };
    Board(AddressT board_hw_address, std::shared_ptr<VoltagesQ> data_queue, std::shared_ptr<Mutex> sequential_run_mutex)
      : dataLink{ board_hw_address }
      , console{ "IOBoard:" + std::to_string(board_hw_address) + "::", ProjCfg::EnableLogForComponent::IOBoards }
      , allPinsVoltagesTableQueue{ std::move(data_queue) }
      , getAllPinsVoltagesSemaphore{ std::make_shared<Semaphore>() }
      , sequentialRunMutex{ std::move(sequential_run_mutex) }
      , getAllPinsVoltagesTask([this]() { GetAllPinsVoltagesTask(); },
                              ProjCfg::Tasks::VoltageCheckTaskStackSize,
                              ProjCfg::Tasks::VoltageCheckTaskPio,
                              "board" + std::to_string(board_hw_address),
                              true)
    {
        getAllPinsVoltagesTask.Start();
    }

    static PinNumT GetHarnessPinNumFromLogicPinNum(PinNumT logic_pin_num) noexcept
    {
        if (logic_pin_num > pinCount)
            std::terminate();

        return logicPinToPinOnBoardMapping.at(logic_pin_num);
    }
    static PinNumT GetLogicPinNumFromHarnessPinNum(PinNumT harness_pin_num) noexcept
    {
        if (harness_pin_num > pinCount)
            std::terminate();

        return harnessToLogicPinNumMapping.at(harness_pin_num);
    }
    [[nodiscard]] AddressT                   GetAddress() const noexcept { return dataLink.GetAddress(); }
    [[nodiscard]] std::shared_ptr<Semaphore> GetStartSemaphore() const noexcept { return getAllPinsVoltagesSemaphore; }
    Result                                   SetVoltageAtPin(PinNumT pin, int retry_times = 0) noexcept
    {
        auto result = SendCmd(ToUnderlying(Command::SetPinVoltage), CommandArgT{ static_cast<Byte>(pin) }, retry_times);

        if (result != Result::Good) {
            console.LogError("Voltage setting on pin " + std::to_string(pin) + " unsuccessful");
        }

        return result;
    }
    Result SetOutputVoltageValue(OutputVoltage value, int retry_times = 0) noexcept
    {
        auto result =
          SendCmd(ToUnderlying(Command::SetOutputVoltage), CommandArgT{ static_cast<Byte>(value) }, retry_times);

        if (result != Result::Good)
            console.LogError("Voltage setting unsuccessful");
        else {
            switch (value) {
            case OutputVoltage::_07: outVoltageLevelSetting = ProjCfg::LOW_OUTPUT_VOLTAGE_VALUE; break;
            case OutputVoltage::_09: outVoltageLevelSetting = ProjCfg::HIGH_OUTPUT_VOLTAGE_VALUE; break;
            case OutputVoltage::Undefined: outVoltageLevelSetting = ProjCfg::LOW_OUTPUT_VOLTAGE_VALUE; break;
            }
        }

        return result;
    }
    Result DisableOutput(int retry_times = 0) noexcept
    {
        return SetVoltageAtPin(
          static_cast<std::underlying_type_t<VoltageSetCmd::Special>>(VoltageSetCmd::Special::DisableAll),
          retry_times);
    }
    bool StartTest(int retry_times = 0) noexcept
    {
        auto result = SendCmd(ToUnderlying(Command::StartTest), retry_times);
        if (result != Result::Good) {
            console.LogError("Test Start Unsuccessful!");
        }

        return false;
    }
    std::pair<Result, std::optional<bool>> CheckIfFirmwareVersionIsCompliant(int retry_times = 0) noexcept
    {
        auto res =
          SendCmdAndReadResponse<Byte>(GetFirmwareVersion::cmd, GetFirmwareVersion::delayForResponseMs, retry_times);

        if (res.first == Result::Good) {
            if (res.second == GetFirmwareVersion::targetVersion)
                return { res.first, true };
            else
                return { res.first, false };
        }
        else
            return { res.first, std::nullopt };
    }
    Result SetInternalParameters(SetInternalParametersCmd::InternalParamsT params, int retry_times = 0) noexcept
    {
        auto buffer = reinterpret_cast<std::array<Byte, sizeof(params)> *>(&params);

        for (auto const byte : *buffer) {
            auto send_result = SendCmdAndReadResponse<BoardAnswer>(SetInternalParametersCmd::cmd,
                                                                   byte,
                                                                   SetInternalParametersCmd::delayForResponseMs,
                                                                   retry_times);

            if (send_result.first != Result::Good)
                return send_result.first;
            if (*send_result.second != BoardAnswer::REPEAT_CMD_TO_CONFIRM) {
                console.LogError("SetInternalParams::board did not sent REPEAT CMD TO CONFIRM, sent: " +
                                 std::to_string(ToUnderlying(*send_result.second)));
                return Result::UnhealthyAnswerValue;
            }
        }

        auto last_send_result = SendCmd(SetInternalParametersCmd::cmd);
        if (last_send_result != Result::Good) {
            console.LogError("SetInternalParameters::last send unsuccessful");
        }

        Task::DelayMs(SetInternalParametersCmd::delayForResponseMs);

        auto read_result = ReadResponse<BoardAnswer>(retry_times);
        if (read_result.first != Result::Good) {
            console.LogError("SetInternalParameters::last read unsuccessful!");
            return read_result.first;
        }

        if (*read_result.second != BoardAnswer::OK) {
            console.LogError("SetInternalParameters::Last send board answer is not OK: " +
                             std::to_string(ToUnderlying(*read_result.second)));
            return Result::UnhealthyAnswerValue;
        }

        return Result::Good;
    }
    std::pair<Result, std::optional<GetInternalParametersCmd::InternalParamsT>> GetInternalParameters(
      int retry_times = 0) noexcept
    {
        auto send_result = SendCmd(GetInternalParametersCmd::cmd);

        if (send_result != Result::Good) {
            console.LogError("GetInternals::send result is not good!");
        }

        Task::DelayMs(GetInternalParametersCmd::delayForResponseMs);

        auto read_result = ReadResponse<std::array<uint16_t, SetInternalParametersCmd::numberOfParams>>();

        if (read_result.first != Result::Good) {
            console.LogError("GetInternalParams::bad read:" + std::to_string(ToUnderlying(read_result.first)));
            return { read_result.first, std::nullopt };
        }

        GetInternalParametersCmd::InternalParamsT retval;
        retval.inputResistance1  = read_result.second->at(0);
        retval.outputResistance1 = read_result.second->at(1);
        retval.inputResistance2  = read_result.second->at(2);
        retval.outputResistance2 = read_result.second->at(3);
        retval.shuntResistance   = read_result.second->at(4);
        retval.outputVoltageLow  = read_result.second->at(5);
        retval.outputVoltageHigh = read_result.second->at(6);

        return { read_result.first, retval };
    }
    static VoltageT CalculateVoltageFromAdcValue(Board::ADCValueT adc_value) noexcept
    {
        VoltageT constexpr reference = 1.1;

        return (static_cast<VoltageT>(adc_value) / 1024) * reference;
    }
    ResistanceT CalculateConnectionResistanceFromAdcValue(Board::ADCValueT adc_value) noexcept
    {
        CircuitParamT constexpr output_resistance = 210;
        CircuitParamT constexpr input_resistance  = 1100;
        CircuitParamT constexpr shunt_resistance  = 330;

        auto voltage            = CalculateVoltageFromAdcValue(adc_value);
        auto circuit_current    = voltage / shunt_resistance;
        auto overall_resistance = outVoltageLevelSetting / circuit_current;
        auto test_resistance    = overall_resistance - output_resistance - input_resistance - shunt_resistance;
        return test_resistance;
    }

    std::pair<Result, std::optional<InternalCounterT>> GetBoardCounterValue(int retry_times = 0) noexcept
    {
        auto [comm_result, response] =
          SendCmdAndReadResponse<InternalCounterT>(GetInternalCounter::command,
                                                   GetInternalCounter::delayBeforeResultCheck,
                                                   retry_times);

        if (comm_result != Result::Good) {
            console.LogError("Get counter value command not succeeded");
            return { comm_result, std::nullopt };
        }

        if (*response == UINT32_MAX or *response == 0) {
            console.LogError("Board did not answer properly for GetInternalCounter command, result is:" +
                             std::to_string(*response));
            return { Result::UnhealthyAnswerValue, std::nullopt };
        }

        return { comm_result, response };
    }
    std::pair<Result, std::optional<ADCValueT>> GetPinVoltage(Byte pin, int retry_times = 0) noexcept
    {
        auto [comm_result, response] = SendCmdAndReadResponse<ADCValueT>(static_cast<Byte>(Command::GetPinVoltage),
                                                                         CommandArgT{ pin },
                                                                         VoltageCheckCmd::timeToWaitForResponseOnePinMs,
                                                                         retry_times);

        if (comm_result != Result::Good) {
            console.LogError("Voltage check command for pin " + std::to_string(pin) + " unsuccessful");
            return { comm_result, std::nullopt };
        }

        return { comm_result, response };
    }
    std::pair<Result, std::optional<AllPinsVoltages8B>> GetAllPinsVoltages(int retry_times = 0) noexcept
    {
        auto [comm_result, voltages] =
          SendCmdAndReadResponse<AllPinsVoltages8B>(static_cast<Byte>(Command::GetPinVoltage),
                                                    CommandArgT{ VoltageCheckCmd::SpecialMeasurements::MeasureAll },
                                                    VoltageCheckCmd::timeToWaitForResponseAllPinsMs);

        if (comm_result != Result::Good) {
            console.LogError("Reading all pins voltages unsuccessful");
            return { comm_result, std::nullopt };
        }

        return { comm_result, voltages };
    }
    Result SetNewBoardAddress(AddressT new_address)
    {
        if (new_address < ProjCfg::BoardsConfigs::MinAddress or new_address > ProjCfg::BoardsConfigs::MaxAddress) {
            throw std::invalid_argument("address value is out of range!");
        }

        auto new_address_command = SetNewBoardAddressCmd{ new_address };
        auto args                = new_address_command.GetAllArguments();

        int       stage_counter     = 0;
        const int last_argument_num = args.size() - 1;
        for (auto const &arg : args) {
            Task::DelayMs(50);

            auto send_result = SendCmd(SetNewBoardAddressCmd::command, arg);
            Task::DelayMs(SetNewBoardAddressCmd::delayBeforeResultCheckMs);

            if (send_result != Result::Good) {
                if (stage_counter == last_argument_num) {
                    break;
                }
                else {
                    return Result::BadCommunication;
                }
            }

            auto [read_result, answer] = dataLink.ReadBoardAnswer<Byte>();

            if (read_result != DataLink::Result::Good)
                return Result::BadCommunication;

            if (*answer == BoardAnswer::REPEAT_CMD_TO_CONFIRM) {
                stage_counter++;
                continue;
            }
            else if (*answer == BoardAnswer::FAIL) {
                console.LogError("Fail arrived");
                return Result::BoardAnsweredFail;
            }
            else {
                console.LogError("Unknown answer arrived: " + std::to_string(*answer));
                return Result::BadAnswerFormat;
            }
        }

        Task::DelayMs(50);
        dataLink.SetNewAddress(new_address);
        Task::DelayMs(SetNewBoardAddressCmd::delayBeforeResultCheckMs);
        auto [read_result, answer] = dataLink.ReadBoardAnswer<Byte>();

        if (read_result != DataLink::Result::Good) {
            return Result::BadCommunication;
        }

        if (*answer == BoardAnswer::OK) {
            console.Log("Board address change success");
            return Result::Good;
        }
        else if (*answer == BoardAnswer::FAIL) {
            console.LogError("Fail arrived");
            return Result::BoardAnsweredFail;
        }
        else {
            console.LogError("Unknown answer arrived: " + std::to_string(*answer));
            return Result::BadAnswerFormat;
        }
    }

    std::optional<std::vector<Byte>> UnitTestCommunication(auto const &data) noexcept
    {
        auto response = dataLink.UnitTestCommunication(data);

        if (not response)
            return std::nullopt;

        if (data != *response) {
            console.LogError("slave answer is different from original data: ");

            auto counter = 0;

            for (auto const value : data) {
                console.LogError("orig: " + std::to_string(value) +
                                 " || response: " + std::to_string(response->at(counter)));

                counter++;
            }

            return std::nullopt;
        }
        else {
            console.Log("slave responded successfully");
        }

        return response;
    }

  protected:
    Result SendCmd(Byte cmd, CommandArgT args, int retry_times = 0) noexcept
    {
        int retry_counter = 0;

        DataLink::Result result;
        do {
            result = dataLink.SendCommandAndCheckAcknowledge(cmd, args);
            if (result == DataLink::Result::Good)
                return Result::Good;

            if (result == DataLink::Result::BadAcknowledge)
                Task::DelayMs(CommunicationsConfigs::waitToRepeatAfterBadAckMs);
            else
                Task::DelayMs(ProjCfg::BoardsConfigs::DelayBeforeRetryCommandSendMs);

            retry_counter++;
        } while (retry_counter < retry_times);

        if (result == DataLink::Result::BadAcknowledge)
            return Result::BadAcknowledge;
        else
            return Result::BadCommunication;
    }
    Result SendCmd(Byte cmd, int retry_times = 0) noexcept { return SendCmd(cmd, CommandArgT{}, retry_times); }
    template<typename ReturnType>
    std::pair<Result, std::optional<ReturnType>> ReadResponse(int retry_times = 0) noexcept
    {
        DataLink::Result result;

        int retry_counter = 0;
        do {
            auto [read_result, value] = dataLink.ReadBoardAnswer<ReturnType>();
            result                    = read_result;

            if (read_result == DataLink::Result::Good)
                return { Result::Good, value };

            retry_counter++;
        } while (retry_counter < retry_times);

        if (result == DataLink::Result::BadCommunication)
            return { Result::BadCommunication, std::nullopt };
        if (result == DataLink::Result::BadAcknowledge)
            return { Result::BadAcknowledge, std::nullopt };
        else
            return { Result::BadCommunication, std::nullopt };
    }

    template<typename ReturnType>
    std::pair<Result, std::optional<ReturnType>> SendCmdAndReadResponse(Byte        cmd,
                                                                        CommandArgT args,
                                                                        size_t      delay_for_response_ms,
                                                                        int         retry_times = 0) noexcept
    {
        auto send_result = SendCmd(cmd, args, retry_times);

        if (send_result != Result::Good) {
            console.LogError("Unsuccessful command sending (" + std::to_string(cmd) + ")");
            return { send_result, std::nullopt };
        }

        Task::DelayMsUntil(delay_for_response_ms);

        return ReadResponse<ReturnType>(retry_times);
    }
    template<typename ReturnType>
    std::pair<Result, std::optional<ReturnType>> SendCmdAndReadResponse(Byte   cmd,
                                                                        size_t delay_for_response_ms,
                                                                        int    retry_times = 0) noexcept
    {
        return SendCmdAndReadResponse<ReturnType>(cmd, CommandArgT{}, delay_for_response_ms, retry_times);
    }

    // tasks
    [[noreturn]] void GetAllPinsVoltagesTask() noexcept
    {
        while (true) {
            getAllPinsVoltagesSemaphore->Take_BlockInfinitely();

            {
                sequentialRunMutex->lock();
                auto [comm_result, voltages] = GetAllPinsVoltages();

                if (comm_result != Result::Good) {
                    allPinsVoltagesTableQueue->Send(
                      OneBoardVoltages{ comm_result, dataLink.GetAddress(), AllPinsVoltages8B{} });

                    sequentialRunMutex->unlock();
                    continue;
                }

                Result operation_result = Result::Good;
                int    pin_counter      = 0;
                for (auto const voltage : *voltages) {
                    if (voltage == UINT8_MAX) {
                        console.LogError("voltage value at pin " + std::to_string(pin_counter) +
                                         " is clipped(maxed, =255)");
                        operation_result = Result::UnhealthyAnswerValue;
                        break;
                    }

                    if (voltage > VoltageCheckCmd::rawHealthyVoltageValueIsBelow) {
                        console.LogError("voltage level is suspiciously high:" + std::to_string(voltage));
                        operation_result = Result::UnhealthyAnswerValue;
                        break;
                    }

                    pin_counter++;
                }

                if (operation_result != Result::Good) {
                    sequentialRunMutex->unlock();
                    continue;
                }

                operation_result = Result::Good;
                allPinsVoltagesTableQueue->Send(OneBoardVoltages{ operation_result, dataLink.GetAddress(), *voltages });
                sequentialRunMutex->unlock();
            }
        }
    }

  private:
    struct ReturnType {
        uint32_t someInt;
        uint32_t secondInt;
        float    someFloat;
    };
    struct CommunicationsConfigs {
        auto constexpr static waitToRepeatAfterBadAckMs = 200;
    };

    DataLink                   dataLink;
    SmartLogger                console;

    std::shared_ptr<VoltagesQ> allPinsVoltagesTableQueue;
    std::shared_ptr<Semaphore> getAllPinsVoltagesSemaphore;
    std::shared_ptr<Mutex>     sequentialRunMutex;

    Task                       getAllPinsVoltagesTask;

    OutputVoltageRealT         outVoltageLevelSetting = ProjCfg::DEFAULT_OUTPUT_VOLTAGE_VALUE;
};