#pragma once
#include <cstdlib>
#include <memory>
#include <optional>

#include "esp_logger.hpp"
#include "iic.hpp"
#include "task.hpp"
#include "utilities.hpp"

class DataLink {
  public:
    using Byte         = uint8_t;
    using AddressT     = Byte;
    using CommandT     = Byte;
    using CommandArgsT = Byte;
    using IIC_Result   = IIC::OperationResult;
    using UnitTestType = std::array<Byte, 10>;

    enum class Result {
        BadCommunication,
        BadAcknowledge,
        FlushFailed,
        Good
    };

    DataLink(AddressT board_address)
      : logger{ "dataLink, Addr:" + std::to_string(board_address), ProjCfg::EnableLogForComponent::IOBoards }
      , driver{ IIC::Get() }
      , boardAddress{ board_address }
    { }

    AddressT GetAddress() const noexcept { return boardAddress; }
    void     SetNewAddress(AddressT new_address) noexcept
    {
        boardAddress = new_address;
        logger.SetNewTag(std::to_string(new_address));
    }

    Result SendCommandAndCheckAcknowledge(CommandT cmd, CommandArgsT args)
    {
        // read before write to make sure output buffer is empty
        auto flush_result = FlushOutputIOBoardBuffer();
        if (flush_result != Result::Good)
            return flush_result;
        // write command
        auto write_result = driver->Write(boardAddress, std::vector{ cmd, args }, xferTimeout);

        if (write_result != IIC_Result::OK)
            return Result::BadCommunication;

        Task::DelayMs(delayBeforeCommandAckCheck);

        // read answer, respected commands are acknowledged with reversed bits in data byte
        auto [read_result, response] = driver->Read<CommandAndArgs>(boardAddress, xferTimeout);
        if (read_result != IIC_Result::OK)
            return Result::BadCommunication;

        if (not CheckIfBoardRespectedCommand(cmd, response->cmd))
            return Result::BadAcknowledge;

        if (args != response->args) {
            logger.LogError("Board bad command arguments acknowledge: command: " + std::to_string(cmd) + " arguments: " +
                            std::to_string(args) + ". Args response: " + std::to_string(response->args));

            return Result::BadAcknowledge;
        }

        return Result::Good;
    }
    template<typename ReturnType>
    std::pair<Result, std::optional<ReturnType>> ReadBoardAnswer() noexcept
    {
        auto [result, retvalue] = driver->Read<ReturnType>(boardAddress, xferTimeout);

        if (result == IIC_Result::OK)
            return {Result::Good, retvalue};
        else
            return {Result::BadCommunication, std::nullopt};
    }

    std::optional<std::vector<Byte>> UnitTestCommunication(std::vector<Byte> const &data) noexcept
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

  protected:
    struct CommandAndArgs {
        CommandT     cmd;
        CommandArgsT args;
    };
    Result FlushOutputIOBoardBuffer() noexcept
    {
        auto try_num = flushReadsMaxCount;

        while (try_num--) {
            auto [result, value] = driver->Read<Byte>(boardAddress, xferTimeout);

            if (result != IIC_Result::OK)
                return Result::BadCommunication;

            if (*value == valueIndicatesEmptyBoardOutputBuffer)
                return Result::Good;
        }
        logger.LogError("buffer flush unsuccessful, attempted to flush " + std::to_string(flushReadsMaxCount) +
                        " number of times");

        return Result::FlushFailed;
    }
    [[nodiscard]] bool CheckIfBoardRespectedCommand(Byte command, Byte response) noexcept
    {
        auto expected_result = ReverseBits(command);

        if (expected_result != response) {
            logger.LogError("Board bad command acknowledge, expected:" + std::to_string(expected_result) +
                            " obtained:" + std::to_string(response));

            return false;
        }
        else
            logger.Log("Board respected command:" + std::to_string(command));

        return true;
    }

    [[nodiscard]] constexpr Byte ReverseBits(Byte data) const noexcept { return ~data; }

  private:
    auto constexpr static xferTimeout                          = 500;
    auto constexpr static flushReadsMaxCount                   = 100;
    auto constexpr static valueIndicatesEmptyBoardOutputBuffer = 0xff;
    auto constexpr static delayBeforeCommandAckCheck           = ToUnderlying(ProjCfg::BoardsConfigs::DelayBeforeAcknowledgeCheckMs);
    Logger               logger;
    std::shared_ptr<IIC> driver;
    AddressT             boardAddress;
};