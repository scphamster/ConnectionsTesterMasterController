#pragma once
#include <cstdlib>
#include <optional>

#include "data_link.hpp"
#include "queue.hpp"
#include "task.hpp"
#include "semaphore.hpp"
#include "utilities.hpp"

// todo: implement internal timer to not bother user about delays
class Board {
  public:
    auto static constexpr pinCount = ProjCfg::BoardsConfigs::NumberOfPins;

    using Byte              = uint8_t;
    using PinNumT           = size_t;
    using AddressT          = Byte;
    using ADCValueT         = uint16_t;
    using AdcValueLoRes     = Byte;
    using InternalCounterT  = uint32_t;
    using CommandArgsT      = Byte;
    using AllPinsVoltages   = std::array<ADCValueT, pinCount>;
    using AllPinsVoltages8B = std::array<AdcValueLoRes, pinCount>;

    enum class Result {
        BadCommunication,
        BadAcknowledge,
        BadAnswerFormat,
        UnhealthyAnswerValue,
        Good,
        BoardAnsweredFail
    };

    struct PinsVoltages {
        Result            readResult;
        AddressT          boardAddress;
        AllPinsVoltages8B voltagesArray;
    };
    using VoltagesQ = Queue<PinsVoltages>;
    enum class Command {
        SetPinVoltage      = 0xC1,
        GetInternalCounter = 0xC2,
        GetPinVoltage      = 0xC3,
        SetOutputVoltage   = 0xc4,
        StartTest          = 0xc6,
    };
    enum BoardAnswer {
        OK   = 0xa5,
        FAIL = 0x50
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
        PinNum pin;
    };
    struct VoltageSetCmd {
        enum class Special {
            DisableAll = 254
        };
    };
    struct SetOwnAddressCmd {
        Byte static constexpr command                  = 0xc5;
        size_t static constexpr delayBeforeResultCheck = 500;
    };
    struct GetInternalCounter {
        Byte static constexpr command                  = ToUnderlying(Command::GetInternalCounter);
        size_t static constexpr delayBeforeResultCheck = 100;
    };
    Board(AddressT board_hw_address, std::shared_ptr<VoltagesQ> data_queue)
      : dataLink{ board_hw_address }
      , console{ "IOBoard, addr " + std::to_string(board_hw_address) + ':', ProjCfg::EnableLogForComponent::IOBoards }
      , allPinsVoltagesTableQueue{ std::move(data_queue) }
      , getAllPinsVoltagesSemaphore{ std::make_shared<Semaphore>() }
      , voltageTableCheckTask([this]() { GetAllPinsVoltagesTask(); },
                              ProjCfg::Tasks::VoltageCheckTaskStackSize,
                              ProjCfg::Tasks::VoltageCheckTaskPio,
                              "board" + std::to_string(board_hw_address),
                              true)
    {
        voltageTableCheckTask.Start();
    }
    [[nodiscard]] AddressT                   GetAddress() const noexcept { return dataLink.GetAddress(); }
    [[nodiscard]] std::shared_ptr<Semaphore> GetStartSemaphore() const noexcept { return getAllPinsVoltagesSemaphore; }
    void   SetNewAddress(AddressT i2c_address) noexcept { dataLink.SetNewAddress(i2c_address); }
    Result SetVoltageAtPin(PinNumT pin, int retry_times = 0) noexcept
    {
        auto result = SendCmd(ToUnderlying(Command::SetPinVoltage), CommandArgsT{ static_cast<Byte>(pin) }, retry_times);

        if (result != Result::Good) {
            console.LogError("Voltage setting on pin " + std::to_string(pin) + " unsuccessful");
        }

        return result;
    }
    Result SetOutputVoltageValue(OutputVoltage value, int retry_times = 0) noexcept
    {
        auto result =
          SendCmd(ToUnderlying(Command::SetOutputVoltage), CommandArgsT{ static_cast<Byte>(value) }, retry_times);

        if (result != Result::Good) {
            console.LogError("Voltage setting unsuccessful");
        }

        return result;
    }
    void DisableOutput(int retry_times = 0)
    {
        SetVoltageAtPin(static_cast<std::underlying_type_t<VoltageSetCmd::Special>>(VoltageSetCmd::Special::DisableAll),
                        retry_times);
    }
    bool StartTest(int retry_times = 0)
    {
        auto result = SendCmd(ToUnderlying(Command::StartTest), retry_times);
        if (result != Result::Good) {
            console.LogError("Test Start Unsuccessful!");
        }

        return false;
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
                                                                         CommandArgsT{ pin },
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
                                                    CommandArgsT{ VoltageCheckCmd::SpecialMeasurements::MeasureAll },
                                                    VoltageCheckCmd::timeToWaitForResponseAllPinsMs);

        if (comm_result != Result::Good) {
            console.LogError("Reading all pins voltages unsuccessful");
            return { comm_result, std::nullopt };
        }

        return { comm_result, voltages };
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
    Result SetNewBoardAddress(AddressT new_address)
    {
        if (new_address < ProjCfg::BoardsConfigs::MinAddress or new_address > ProjCfg::BoardsConfigs::MaxAddress) {
            throw std::invalid_argument("address value is out of range!");
        }

        // todo: add check if succeeded
        SendCmd(SetOwnAddressCmd::command, new_address);

        SetNewAddress(new_address);

        Task::DelayMs(SetOwnAddressCmd::delayBeforeResultCheck);

        auto [read_result, answer] = dataLink.ReadBoardAnswer<Byte>();

        if (read_result != DataLink::Result::Good)
            return Result::BadCommunication;

        if (*answer == BoardAnswer::OK) {
            console.Log("Ok arrived");
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

  protected:
    Result SendCmd(Byte cmd, CommandArgsT args, int retry_times = 0) noexcept
    {
        int retry_counter = 0;

        DataLink::Result result;
        do {
            result = dataLink.SendCommandAndCheckAcknowledge(cmd, args);
            if (result == DataLink::Result::Good)
                return Result::Good;

            Task::DelayMs(ProjCfg::BoardsConfigs::DelayBeforeRetryCommandSendMs);
            retry_counter++;
        } while (retry_counter < retry_times);

        if (result == DataLink::Result::BadAcknowledge)
            return Result::BadAcknowledge;

        return Result::BadCommunication;
    }
    Result SendCmd(Byte cmd, int retry_times = 0) noexcept { return SendCmd(cmd, CommandArgsT{}, retry_times); }
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
    std::pair<Result, std::optional<ReturnType>> SendCmdAndReadResponse(Byte         cmd,
                                                                        CommandArgsT args,
                                                                        size_t       delay_for_response_ms,
                                                                        int          retry_times = 0) noexcept
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
        return SendCmdAndReadResponse<ReturnType>(cmd, CommandArgsT{}, delay_for_response_ms, retry_times);
    }

    // tasks
    [[noreturn]] void GetAllPinsVoltagesTask() noexcept
    {
        while (true) {
            getAllPinsVoltagesSemaphore->Take_BlockInfinitely();

            auto [comm_result, voltages] = GetAllPinsVoltages();

            if (comm_result != Result::Good) {
                allPinsVoltagesTableQueue->Send(PinsVoltages{ comm_result, dataLink.GetAddress(), AllPinsVoltages8B{} });
                continue;
            }

            Result operation_result = Result::BoardAnsweredFail;
            int    pin_counter      = 0;
            for (auto const voltage : *voltages) {
                if (voltage == UINT8_MAX) {
                    console.LogError("voltage value at pin " + std::to_string(pin_counter) + " is clipped(maxed, =255)");
                    operation_result = Result::UnhealthyAnswerValue;
                }

                pin_counter++;
            }

            operation_result = Result::Good;
            allPinsVoltagesTableQueue->Send(PinsVoltages{ operation_result, dataLink.GetAddress(), *voltages });
        }
    }

  private:
    struct ReturnType {
        uint32_t someInt;
        uint32_t secondInt;
        float    someFloat;
    };
    DataLink                   dataLink;
    SmartLogger                console;
    std::shared_ptr<VoltagesQ> allPinsVoltagesTableQueue;
    std::shared_ptr<Semaphore> getAllPinsVoltagesSemaphore;
    Task                       voltageTableCheckTask;
};