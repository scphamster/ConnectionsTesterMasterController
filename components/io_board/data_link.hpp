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
    using CommandArgsT = std::vector<Byte>;
    using IIC_Result   = IIC::OperationResult;

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
        auto result = driver->Write(boardAddress, std::vector{ cmd }, xferTimeout);

        if (result != IIC_Result::OK) {
            return false;
        }

        Task::DelayMs(delayBeforeCmdAckCheck);

        // read answer, respected commands are acknowledged with reversed bits in data byte
        auto response = driver->Read<Byte>(boardAddress, xferTimeout);
        if (not response)
            return false;

        if (not CheckIfBoardRespectedCommand(cmd, *response)) {
            logger.LogError("slave has not respected command: " + std::to_string(cmd) +
                            ", and answered: " + std::to_string(*response));
            return false;
        }
        else {
            logger.Log("slave respected command: " + std::to_string(cmd));
        }

//        Task::DelayMs(10);

        // write arguments
        if (args.size() == 0)
            return true;

        result = driver->Write(boardAddress, args, xferTimeout);
        Task::DelayMs(delayBeforeArgsAckCheck);

        auto args_answer = driver->Read(boardAddress, args.size(), xferTimeout);
        if (not args_answer) {
            logger.LogError("Error upon getting response to arguments for command: " + std::to_string(cmd));
            return false;
        }

        if (args != *args_answer) {
            std::string received_args;
            for (auto const arg : *args_answer) {
                received_args += std::to_string(arg) += ' ';
            }

            std::string sent_args;
            for (auto const arg : args) {
                sent_args += std::to_string(arg);
            }

            logger.LogError("Board has not acknowledged arguments for command: " + std::to_string(cmd) +
                            " (arguments: " + sent_args + "). Responded to arguments with: " + received_args);

            return false;
        }

        return true;
    }

    template<typename ReturnType>
    std::optional<ReturnType> ReadBoardAnswer() noexcept
    {
        return driver->Read<ReturnType>(boardAddress, xferTimeout);
    }

  protected:
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
    auto constexpr static delayBeforeCmdAckCheck               = 5;
    auto constexpr static delayBeforeArgsAckCheck              = 3;
    SmartLogger          logger;
    std::shared_ptr<IIC> driver;
    AddressT             boardAddress;
};