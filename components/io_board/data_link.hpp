#pragma once
#include <cstdlib>
#include <memory>

#include "esp_logger.hpp"
#include "iic.hpp"
#include "task.hpp"

class DataLink {
  public:
    using Byte         = uint8_t;
    using AddressT     = Byte;
    using CommandT     = Byte;
    using CommandArgsT = std::vector<Byte>;
    using IIC_Result   = IIC::OperationResult;

    DataLink(AddressT board_address)
      : logger{ "dataLink, Addr:" + std::to_string(board_address), ProjCfg::ComponentLogSwitch::IOBoards }
      , driver{ IIC::Get() }
      , boardAddress{ board_address }
    { }

    AddressT GetAddres() const noexcept { return boardAddress; }

    void SetNewAddress(AddressT new_address) noexcept
    {
        boardAddress = new_address;
        logger.SetNewTag(std::to_string(new_address));
    }

    bool SendCommand(CommandT cmd, CommandArgsT args)
    {
        // read before write to make sure output buffer is empty
        if (not FlushOutputIOBoardBuffer())
            return false;

        // write command
        auto result = driver->Write(boardAddress, std::vector{ cmd }, xferTimeout);

        if (result != IIC_Result::OK) {
            return false;
        }

        Task::DelayMs(delayReadAfterWrite);

        // read answer, respected commands are acknowledged with reversed bits in data byte
        auto [read_succeeded, value] = driver->Read<Byte>(boardAddress, xferTimeout);
        if (value != ReverseBits(cmd)) {
            logger.LogError("IO board with address: " + std::to_string(boardAddress) + " did not recognize command: " +
                            std::to_string(cmd) + ", and answered: " + std::to_string(value));
            return false;
        }
        else {
            logger.Log("IO board accepted command: " + std::to_string(cmd));
        }

        // write arguments

        if (args.size() == 0)
            return true;

        driver->Write(boardAddress, args, xferTimeout);
        Task::DelayMs(3);
        auto args_answer = driver->Read(boardAddress, args.size(), xferTimeout);

        std::string args_as_string;
        for (auto const arg : args_answer) {
            args_as_string += std::to_string(arg) += ' ';
        }

        logger.Log("IO answered to arguments with: " + args_as_string);
        return true;
    }

    template<typename ReturnType>
    std::pair<bool, ReturnType> ReadBoardAnswer() noexcept
    {
        return driver->Read<ReturnType>(boardAddress, xferTimeout);
    }

  protected:
    bool FlushOutputIOBoardBuffer() noexcept
    {
        auto try_num = flushReadsMaxCount;

        while (try_num--) {
            auto [read_successful, value] = driver->Read<Byte>(boardAddress, xferTimeout);

            if (not read_successful) {
                return false;
            }
            if (value == emptyBufferIndicatorValue)
                return true;
        }
        logger.LogError("buffer flush unsuccessful, attempted to flush " + std::to_string(flushReadsMaxCount) +
                        " number of times");

        return false;
    }
    Byte ReverseBits(Byte data) const noexcept { return ~data; }

  private:
    auto constexpr static xferTimeout               = 500;
    auto constexpr static flushReadsMaxCount        = 65;
    auto constexpr static emptyBufferIndicatorValue = 0xff;
    auto constexpr static delayReadAfterWrite       = 100;
    SmartLogger          logger;
    std::shared_ptr<IIC> driver;
    AddressT             boardAddress;
};