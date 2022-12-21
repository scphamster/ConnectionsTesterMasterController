#pragma once
#include <cstdlib>
#include <optional>

#include "data_link.hpp"
#include "queue.hpp"
#include "task.hpp"
#include "semaphore.hpp"
class Board {
  public:
    using Byte             = uint8_t;
    using PinNumT          = size_t;
    using AddressT         = Byte;
    using ADCValueT        = uint16_t;
    using InternalCounterT = uint32_t;
    // todo: delete from here after tests
    using CommandArgsT    = std::vector<Byte>;
    using AllPinsVoltages = std::array<uint16_t, 32>;
    struct PinsVoltages {
        AddressT        boardAddress;
        AllPinsVoltages voltagesArray;
    };
    using QueueT = Queue<PinsVoltages>;
    enum class Command {
        GetInternalCounter = 0xC1,
        SetPinVoltage      = 0xC2,
        GetPinVoltage      = 0xC3,
        SetOutputVoltage   = 0xc4
    };
    enum class OutputVoltage : uint8_t {
        Undefined = 0,
        _09       = ProjCfg::high_voltage_reference_select_pin,
        _07       = ProjCfg::low_voltage_reference_select_pin,
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

    AddressT                   GetAddress() const noexcept { return dataLink.GetAddres(); }
    std::shared_ptr<Semaphore> GetStartSemaphore() const noexcept { return getAllPinsVoltagesSemaphore; }
    void                       SetNewAddress(AddressT i2c_address) noexcept { }

    std::optional<InternalCounterT> GetBoardCounterValue() noexcept
    {
        if (not SendCmd(checkTimerCmd)) {
            console.LogError("Get counter value command not succeeded");
            return std::nullopt;
        };

        return dataLink.ReadBoardAnswer<InternalCounterT>();
    }
    std::optional<ADCValueT> GetPinVoltage(Byte pin) noexcept
    {
        auto adc_val = SendCmdAndReadResponse<ADCValueT>(checkADC,
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

    void SetVoltageAtPin(Byte pin) noexcept
    {
        if (not SendCmd(setPinVoltageCmd, CommandArgsT{ pin })) {
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

  protected:
    struct VoltageCheckCmd {
        enum SpecialMeasurements : Byte {
            MeasureAll = 33,
            MeasureVCC,
            MeasureGND
        };
        using PinNum                                               = uint8_t;
        TickType_t static constexpr timeToWaitForResponseAllPinsMs = 1000;
        TickType_t static constexpr timeToWaitForResponseOnePinMs  = 200;
        PinNum pin;
    };
    bool SendCmd(Byte cmd, CommandArgsT args) noexcept { return dataLink.SendCommand(cmd, args); }
    bool SendCmd(Byte cmd) noexcept { return SendCmd(cmd, CommandArgsT{}); }
    template<typename ReturnType>
    std::optional<ReturnType> SendCmdAndReadResponse(Byte cmd, CommandArgsT args, size_t delay_for_response_ms) noexcept
    {
        if (not dataLink.SendCommand(cmd, args)) {
            console.LogError("Command sending not successful, cmd=" + std::to_string(cmd));
            return std::nullopt;
        }

        Task::DelayMs(delay_for_response_ms);

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
            getAllPinsVoltagesSemaphore->TakeBlockInfinitely();

            auto result = GetAllPinsVoltages();

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

    auto constexpr static setPinVoltageCmd = 0xC1;
    auto constexpr static checkTimerCmd    = 0xC2;
    auto constexpr static checkADC         = 0xC3;
};