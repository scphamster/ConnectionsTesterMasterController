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

    using Byte             = uint8_t;
    using PinNumT          = size_t;
    using AddressT         = Byte;
    using ADCValueT        = uint16_t;
    using AdcValueLoRes    = Byte;
    using InternalCounterT = uint32_t;
    using CommandArgsT      = Byte;
    using AllPinsVoltages   = std::array<ADCValueT, pinCount>;
    using AllPinsVoltages8B = std::array<AdcValueLoRes, pinCount>;

    struct PinsVoltages {
        AddressT        boardAddress;
        AllPinsVoltages voltagesArray;
    };
    using QueueT = Queue<PinsVoltages>;
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
        enum SpecialMeasurements : Byte {
            MeasureAll = 33,
            MeasureVCC,
            MeasureGND
        };
        using PinNum                                               = uint8_t;
        TickType_t static constexpr timeToWaitForResponseAllPinsMs = 14;
        TickType_t static constexpr timeToWaitForResponseOnePinMs  = 50;
        PinNum pin;
    };
    struct VoltageSetCmd {
        enum class Special {
            DisableAll = 254
        };
    };
    struct SetOwnAddressCmd {
        Byte static constexpr command                = 0xc5;
        size_t static constexpr waitForResultDelayMs = 500;
    };
    Board(AddressT board_hw_address, std::shared_ptr<QueueT> data_queue)
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
    [[nodiscard]] AddressT                   GetAddress() const noexcept { return dataLink.GetAddres(); }
    [[nodiscard]] std::shared_ptr<Semaphore> GetStartSemaphore() const noexcept { return getAllPinsVoltagesSemaphore; }
    void SetNewAddress(AddressT i2c_address) noexcept { dataLink.SetNewAddress(i2c_address); }
    void SetVoltageAtPin(Byte pin) noexcept
    {
        if (not SendCmd(static_cast<std::underlying_type_t<Command>>(Command::SetPinVoltage), CommandArgsT{ pin })) {
            console.LogError("Voltage setting on pin " + std::to_string(pin) + " unsuccessful");
        }
    }
    void SetOutputVoltageValue(OutputVoltage value) noexcept
    {
        if (not SendCmd(static_cast<std::underlying_type_t<Command>>(Command::SetOutputVoltage),
                        CommandArgsT{ static_cast<Byte>(value) })) {
            console.LogError("Voltage setting unsuccessful");
        }
    }
    void DisableOutput()
    {
        SetVoltageAtPin(static_cast<std::underlying_type_t<VoltageSetCmd::Special>>(VoltageSetCmd::Special::DisableAll));
    }
    bool StartTest()
    {
        auto result = SendCmd(ToUnderlying(Command::StartTest));
        if (not result) {
            console.LogError("Test Start Unsuccessful!");
        }

        return result;
    }

    std::optional<InternalCounterT> GetBoardCounterValue() noexcept
    {
        if (not SendCmd(static_cast<std::underlying_type_t<Command>>(Command::GetInternalCounter))) {
            console.LogError("Get counter value command not succeeded");
            return std::nullopt;
        };

        Task::DelayMs(5);

        return dataLink.ReadBoardAnswer<InternalCounterT>();
    }
    std::optional<ADCValueT> GetPinVoltage(Byte pin) noexcept
    {
        auto adc_val = SendCmdAndReadResponse<ADCValueT>(static_cast<Byte>(Command::GetPinVoltage),
                                                         CommandArgsT{ pin },
                                                         VoltageCheckCmd::timeToWaitForResponseOnePinMs);
        if (not adc_val) {
            console.LogError("Voltage check command for pin " + std::to_string(pin) + " unsuccessful");
            return std::nullopt;
        }

        return adc_val;
    }
    std::optional<AllPinsVoltages> GetAllPinsVoltages() noexcept
    {
        auto voltages =
          SendCmdAndReadResponse<AllPinsVoltages>(static_cast<Byte>(Command::GetPinVoltage),
                                                  CommandArgsT{ VoltageCheckCmd::SpecialMeasurements::MeasureAll },
                                                  VoltageCheckCmd::timeToWaitForResponseAllPinsMs);

        if (not voltages) {
            console.LogError("Reading all pins voltage unsuccessful");
            return std::nullopt;
        }

        return voltages;

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
    bool SetNewBoardAddress(AddressT new_address)
    {
        if (new_address < ProjCfg::BoardsConfigs::MinAddress or new_address > ProjCfg::BoardsConfigs::MaxAddress) {
            throw std::invalid_argument("address value is out of range!");
        }

        auto send_succeeded = SendCmd(SetOwnAddressCmd::command, new_address);

        //        if (not send_succeeded) return false;

        SetNewAddress(new_address);

        Task::DelayMs(SetOwnAddressCmd::waitForResultDelayMs);

        auto result = dataLink.ReadBoardAnswer<Byte>();

        if (result) {
            if (*result == BoardAnswer::OK) {
                console.Log("Ok arrived");
                return true;
            }
            else if (*result == BoardAnswer::FAIL) {
                console.LogError("Fail arrived");
                return false;
            }
            else {
                console.LogError("Unknown answer arrived: " + std::to_string(*result));
                return false;
            }
        }
        else
            return false;
    }

  protected:
    bool SendCmd(Byte cmd, CommandArgsT args) noexcept { return dataLink.SendCommand(cmd, args); }
    bool SendCmd(Byte cmd) noexcept { return SendCmd(cmd, CommandArgsT{}); }
    template<typename ReturnType>
    std::optional<ReturnType> SendCmdAndReadResponse(Byte cmd, CommandArgsT args, size_t delay_for_response_ms) noexcept
    {
        if (not dataLink.SendCommand(cmd, args)) {
            console.LogError("Command sending not successful, cmd=" + std::to_string(cmd));
            return std::nullopt;
        }

        Task::DelayMsUntil(delay_for_response_ms);

        return dataLink.ReadBoardAnswer<ReturnType>();
    }
    template<typename ReturnType>
    std::optional<ReturnType> SendCmdAndReadResponse(Byte cmd, size_t delay_for_response_ms) noexcept
    {
        return SendCmdAndReadResponse<ReturnType>(cmd, CommandArgsT{}, delay_for_response_ms);
    }

    // tasks
    [[noreturn]] void GetAllPinsVoltagesTask() noexcept
    {
        while (true) {
            getAllPinsVoltagesSemaphore->Take_BlockInfinitely();

            auto result = GetAllPinsVoltages();

            if (result == std::nullopt) {
                console.LogError("board with adress: " + std::to_string(dataLink.GetAddres()) +
                                 " all pins voltages bad result!");
                continue;
            }

            int counter = 0;
            for (const auto &voltage : *result) {
                if (voltage > 1023) {
                    console.LogError("voltage value higher than 1023: " + std::to_string(voltage));
                    if (counter != 0)
                        console.LogError("previous voltage was: " + std::to_string((*result).at(counter - 1)));
                }

                counter++;
            }

            allPinsVoltagesTableQueue->Send(PinsVoltages{ dataLink.GetAddres(), *result });
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
    std::shared_ptr<QueueT>    allPinsVoltagesTableQueue;
    std::shared_ptr<Semaphore> getAllPinsVoltagesSemaphore;
    Task                       voltageTableCheckTask;
};