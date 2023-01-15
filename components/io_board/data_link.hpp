#pragma once
#include <cstdlib>
#include <memory>
#include <optional>

#include "esp_logger.hpp"
#include "iic.hpp"
#include "task.hpp"

class DataLink {
  public:
    using Byte         = uint8_t;
    using AddressT     = Byte;
    using CommandT     = Byte;
    using CommandArgsT = Byte;
    using IIC_Result   = IIC::OperationResult;
    using UnitTestType = std::array<Byte, 10>;

    DataLink(AddressT board_address)
      : logger{ "dataLink, Addr:" + std::to_string(board_address), ProjCfg::EnableLogForComponent::IOBoards }
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
        auto result = driver->Write(boardAddress, std::vector{ cmd, args }, xferTimeout);

        if (result != IIC_Result::OK) {
            return false;
        }

        Task::DelayMs(delayBeforeCommandAckCheck);

        // read answer, respected commands are acknowledged with reversed bits in data byte
        auto response = driver->Read<CommandAndArgs>(boardAddress, xferTimeout);
        if (not response)
            return false;

        if (not CheckIfBoardRespectedCommand(cmd, response->cmd)) {
            logger.LogError("slave has not respected command: " + std::to_string(cmd) +
                            ", and answered: " + std::to_string(response->cmd));
            return false;
        }
        else {
            logger.Log("slave respected command: " + std::to_string(cmd));
        }

        if (args != response->args) {
            logger.LogError("Board has not acknowledged arguments for command: " + std::to_string(cmd) + " (arguments: " +
                            std::to_string(args) + "). Responded to arguments with: " + std::to_string(response->args));

            return false;
        }

        return true;
    }
    std::optional<std::vector<Byte>> UnitTestCommunication(std::vector<Byte> const & data) noexcept
    {
        FlushOutputIOBoardBuffer();

        if (auto result = driver->Write(boardAddress, data, xferTimeout) != IIC_Result::OK) {
            logger.LogError("transfer unsuccessful, error: " + std::to_string(result));
            return std::nullopt;
        }

        Task::DelayMs(100);

        auto retval = driver->Read(boardAddress, data.size(), xferTimeout);

        return retval;
    }
    template<typename ReturnType>
    std::optional<ReturnType> ReadBoardAnswer() noexcept
    {
        return driver->Read<ReturnType>(boardAddress, xferTimeout);
    }

  protected:
    struct CommandAndArgs {
        CommandT     cmd;
        CommandArgsT args;
    };
    bool FlushOutputIOBoardBuffer() noexcept
    {
        auto try_num = flushReadsMaxCount;

        while (try_num--) {
            auto value = driver->Read<Byte>(boardAddress, xferTimeout);

            if (not value) {
                return false;
            }
            if (*value == valueIndicatesEmptyBoardOutputBuffer)
                return true;
        }
        logger.LogError("buffer flush unsuccessful, attempted to flush " + std::to_string(flushReadsMaxCount) +
                        " number of times");

        return false;
    }
    bool CheckIfBoardRespectedCommand(Byte command, Byte response) const noexcept
    {
        return (command == ReverseBits(response)) ? true : false;
    }
    Byte ReverseBits(Byte data) const noexcept { return ~data; }

  private:
    auto constexpr static xferTimeout                          = 500;
    auto constexpr static flushReadsMaxCount                   = 100;
    auto constexpr static valueIndicatesEmptyBoardOutputBuffer = 0xff;
    auto constexpr static delayBeforeCommandAckCheck           = 5;
    SmartLogger          logger;
    std::shared_ptr<IIC> driver;
    AddressT             boardAddress;
};