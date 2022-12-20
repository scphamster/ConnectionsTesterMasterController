#pragma once
#include <cstdlib>
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
        GetPinVoltage      = 0xC3
    };

    Board(AddressT i2c_address, std::shared_ptr<QueueT> data_queue)
      : dataLink{ i2c_address }
      , logger{ EspLogger::Get() }
      , voltageTableQueue{ std::move(data_queue) }
      , getVoltageTableSemaphore{ std::make_shared<Semaphore>() }
      , voltageTableCheckTask([this]() { VoltageCheckTask(); },
                              ProjCfg::Tasks::VoltageCheckTaskStackSize,
                              ProjCfg::Tasks::VoltageCheckTaskPio,
                              "board" + std::to_string(i2c_address),
                              true)
    {
        voltageTableCheckTask.Start();
    }

    AddressT                   GetAddress() const noexcept { return dataLink.GetAddres(); }
    std::shared_ptr<Semaphore> GetStartSemaphore() const noexcept { return getVoltageTableSemaphore; }
    void                       SetNewAddress(AddressT i2c_address) noexcept { }

    std::pair<bool, InternalCounterT> GetBoardCounterValue() noexcept
    {
        if (not SendCmd(checkTimerCmd)) {
            logger->LogError("Get counter value command not succeeded");
            return { false, 0 };
        };

        auto result = dataLink.ReadBoardAnswer<InternalCounterT>();
        return result;
    }
    ADCValueT GetPinVoltage(Byte pin) noexcept
    {
        auto adc_val = SendCmdAndReadResponse<ADCValueT>(checkADC, CommandArgsT{ pin }, 200);

        if (adc_val.first != true) {
            logger->LogError("voltage check unsuccessful");
            return 0;
        }

        logger->Log("Voltage at pin No " + std::to_string(pin) + " = " + std::to_string(adc_val.second));

        return adc_val.second;
    }
    AllPinsVoltages GetAllPinsVoltages() noexcept
    {
        auto adc_val = SendCmdAndReadResponse<AllPinsVoltages>(checkADC, CommandArgsT{ 33 }, 1000);

        if (adc_val.first != true) {
            logger->LogError("Reading all pins voltage unsuccessful");
            return AllPinsVoltages{};
        }

        logger->Log("**********************\nAllPinsVoltages:");

        int pin_num = 0;
        for (auto const &pin_v : adc_val.second) {
            logger->Log("Pin No " + std::to_string(pin_num) + " V = " + std::to_string(pin_v));

            pin_num++;
        }

        return adc_val.second;
    }

    void SetVoltageAtPin(Byte pin) noexcept
    {
        if (not SendCmd(setPinVoltageCmd, CommandArgsT{ pin, 99 })) {
            logger->LogError("Voltage setting on pin " + std::to_string(pin) + " unsuccessful");
        }
    }
    void CheckVoltage() noexcept { }
    bool SendCmd(Byte cmd, CommandArgsT args) noexcept { return dataLink.SendCommand(cmd, args); }

  protected:
    struct VoltageCheckCmd {
        enum SpecialMeasurements {
            MeasureAll = 33,
            MeasureVCC,
            MeasureGND
        };
        using PinNum = uint8_t;
        PinNum pin;
    };
    bool SendCmd(Byte cmd) noexcept { return SendCmd(cmd, CommandArgsT{}); }
    template<typename ReturnType>
    std::pair<bool, ReturnType> SendCmdAndReadResponse(Byte cmd, CommandArgsT args, size_t delay_for_response_ms) noexcept
    {
        if (not dataLink.SendCommand(cmd, args)) {
            logger->LogError("Command sending not successful, cmd=" + std::to_string(cmd));
            return { false, ReturnType{} };
        }

        Task::DelayMs(delay_for_response_ms);

        return dataLink.ReadBoardAnswer<ReturnType>();
    }
    template<typename ReturnType>
    std::pair<bool, ReturnType> SendCmdAndReadResponse(Byte cmd, size_t delay_for_response_ms) noexcept
    {
        return SendCmdAndReadResponse<ReturnType>(cmd, CommandArgsT{}, delay_for_response_ms);
    }

    // tasks
    [[noreturn]] void VoltageCheckTask() noexcept
    {
        while (true) {
            getVoltageTableSemaphore->TakeBlockInfinitely();

            auto result = GetAllPinsVoltages();

            voltageTableQueue->Send(PinsVoltages{ dataLink.GetAddres(), result });

            Task::DelayMs(100);
        }
    }

  private:
    struct ReturnType {
        uint32_t someInt;
        uint32_t secondInt;
        float    someFloat;
    };
    DataLink                   dataLink;
    std::shared_ptr<EspLogger> logger;
    std::shared_ptr<QueueT>    voltageTableQueue;
    std::shared_ptr<Semaphore> getVoltageTableSemaphore;
    Task                       voltageTableCheckTask;

    auto constexpr static setPinVoltageCmd = 0xC1;
    auto constexpr static checkTimerCmd    = 0xC2;
    auto constexpr static checkADC         = 0xC3;
};