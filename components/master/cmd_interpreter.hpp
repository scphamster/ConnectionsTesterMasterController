#pragma once
#include <memory>
#include <vector>

#include "queue.hpp"
#include "esp_logger.hpp"

template<typename MasterDataLink>
class CommandInterpreter {
  public:
    using MasterMsgQueueT = Queue<char>;
    enum class UserCommand {
        MeasureAll,
        EnableOutputForPin,
        SetOutputVoltageLevel,
        CheckConnections,
        CheckResistances,
        CheckVoltages,
        CheckRaw,
        GetAllBoardsIds,
        GetInternalCounter,
        GetTaskStackWatermark,
        SetNewAddressForBoard,
        SetInternalParameters,
        GetInternalParameters,
        Test,
        Unknown
    };
    using StdRetType  = std::pair<UserCommand, std::vector<int>>;
    using StdCmdArgsT = std::vector<std::string>;
    using Byte = uint8_t;

    CommandInterpreter(std::shared_ptr<MasterMsgQueueT> master_msg_queue)
      : console{ "interpreter", ProjCfg::EnableLogForComponent::CommandInterpreter }
      , fromUserInputQ{ std::move(master_msg_queue) }
    { }

    StdRetType WaitForCommand() noexcept
    {
        while (true) {
            std::string cmd;
            cmd += *fromUserInputQ->Receive();

            // make sure whole command arrived, not sure if this needed
            for (;;) {
                auto result = fromUserInputQ->Receive(pdMS_TO_TICKS(10));
                if (result) {
                    cmd += (*result);
                }
                else
                    break;
            }
            console.Log("Command arrived: " + cmd);

            auto words      = StringParser::GetWords(cmd);
            auto first_word = words.at(0);
            if (words.at(0) == "set") {
                int board{ -1 };

                try {
                    board = std::stoi(words.at(1));
                } catch (...) {
                    dataLink->Write("invalid argument: " + words.at(1) + ". Example usage: set 14");
                    return {UserCommand::Unknown, std::vector<int>()};
                }

                int pin;

                try{
                    pin = std::stoi(words.at(2));
                }
                catch(...)
                {
                    dataLink->Write("second argument should be integer of pin number value");
                    return {UserCommand::Unknown, std::vector<int>()};
                }

                return { UserCommand::EnableOutputForPin, std::vector{ board, pin } };
            }
            else if (first_word == "setinternals") {
                return ParseSetInternalParametersCmd(std::move(words));
            }
            else if (first_word == "voltage") {
                if (words.at(1) == "high")
                    return { UserCommand::SetOutputVoltageLevel, std::vector{ 2 } };
                else if (words.at(1) == "low")
                    return { UserCommand::SetOutputVoltageLevel, std::vector{ 1 } };
                else {
                    dataLink->Write("invalid argument for command: " + first_word + ". Respected arguments: low, high");
                    return { UserCommand::Unknown, std::vector<int>() };
                }
            }
            else if (first_word == "check") {
                return ParseCheckConnectionsCmd(words);
            }
            else if (first_word == "getboards") {
                return { UserCommand::GetAllBoardsIds, std::vector{ 0 } };
            }
            else if (first_word == "getinternals") {
                return ParseGetInternalParametersCmd(std::move(words));
            }
            else if (first_word == "counter") {
                if (words.size() == 0) {
                    console.LogError("No board address argument provided for command: 'counter'");
                    continue;
                }

                int board_addr = -1;
                try {
                    board_addr = std::stoi(words.at(1));
                } catch (...) {
                    console.LogError("bad argument for command: 'counter' : " + words.at(1));
                    continue;
                }

                return { UserCommand::GetInternalCounter, std::vector{ board_addr } };
            }
            else if (first_word == "stack") {
                return { UserCommand::GetTaskStackWatermark, std::vector<int>() };
            }
            else if (first_word == "newaddress") {
                int new_addr = -1;
                try {
                    new_addr = std::stoi(words.at(1));
                } catch (...) {
                    dataLink->Write("bad argument for command\n");
                    console.LogError("wrong argument for commmand");
                    return { UserCommand::Unknown, std::vector<int>() };
                }

                auto user_command = UserCommand::SetNewAddressForBoard;

                if (words.size() == 2)
                    return { user_command, std::vector{ new_addr } };

                int current_address = new_addr;

                try {
                    new_addr = std::stoi(words.at(2));
                } catch (...) {
                    dataLink->Write("bad argument for command\n");
                    console.LogError("bad argument for command");
                    return { UserCommand::Unknown, std::vector<int>() };
                }

                return { user_command, std::vector{ current_address, new_addr } };
            }
            else if (first_word == "test") {
                return { UserCommand::Test, std::vector<int>() };
            }

            return { UserCommand::Unknown, std::vector<int>() };
        }
    }

  protected:
    StdRetType ParseCheckConnectionsCmd(std::vector<std::string> words) noexcept
    {
        if (words.size() >= 2) {
            auto second_word  = words.at(1);
            auto user_command = UserCommand();

            if (second_word == "connections")
                user_command = UserCommand::CheckConnections;
            else if (second_word == "resistances")
                user_command = UserCommand::CheckResistances;
            else if (second_word == "voltages")
                user_command = UserCommand::CheckVoltages;
            else if(second_word == "raw")
                user_command = UserCommand::CheckRaw;
            else {
                console.LogError("Unknown command: " + second_word);
                return { UserCommand::Unknown, std::vector<int>() };
            }

            if (words.size() == 2) {
                return { user_command, std::vector<int>() };
            }

            if (words.size() == 3) {
                if (words.at(2) == "sequential") {
                    return { user_command, std::vector<int>{ 1 } };
                }
                else {
                    return { UserCommand::Unknown, std::vector<int>() };
                }
            }

            if (words.size() != 4 and words.size() != 5) {
                console.LogError("Bad 'check' command -> number of arguments is not 2, 3, 4 or 5, size = " +
                                 std::to_string(words.size()));
                return { UserCommand::Unknown, std::vector<int>() };
            }

            int board_argument;
            try {
                board_argument = std::stoi(words.at(2));
            } catch (...) {
                console.LogError("Bad board argument for command 'check': " + words.at(2));
                return { UserCommand::Unknown, std::vector<int>() };
            }

            int pin_argument;
            try {
                pin_argument = std::stoi(words.at(3));
            } catch (...) {
                console.LogError("Bad pin argument for command 'check': " + words.at(3));
                return { UserCommand::Unknown, std::vector<int>() };
            }

            int sequential = 0;
            if (words.size() == 5 && words.at(4) == "sequential")
                sequential = 1;

            return { user_command, std::vector{ board_argument, pin_argument, sequential } };
        }
        else {
            return { UserCommand::Unknown, std::vector<int>() };
        }
    }
    StdRetType ParseSetInternalParametersCmd(StdCmdArgsT words) noexcept
    {
        if (words.size() != Board::SetInternalParametersCmd::numberOfParams + 1) {
            console.LogError("Set internals command Fail: bad args count: " + std::to_string(words.size()));
            return { UserCommand::Unknown, std::vector<int>() };
        }

        std::vector<int> args;

        std::for_each(words.begin() + 1, words.end(), [this, &args](auto const &word) {
            try {
                auto value = std::stoi(word);
                args.push_back(value);
            } catch (...) {
                console.LogError("SetInternalParams fail: argument is not number!:" + word);
            }
        });

        if (args.size() != Board::SetInternalParametersCmd::numberOfParams) {
            return { UserCommand::Unknown, std::vector<int>() };
        }
        else
            return { UserCommand::SetInternalParameters, args };
    }
    StdRetType ParseGetInternalParametersCmd(StdCmdArgsT words) noexcept
    {
        console.Log("GetInternalsCommand:" + words.at(1));

        if (words.size() != 2){
            console.LogError("GetInternalParameters command args num != 2:" + std::to_string(words.size()));
            return { UserCommand::Unknown, std::vector<int>() };
        }

        Byte board_addr;
        try {
            board_addr = std::stoi(words.at(1));
        }
        catch(...)
        {
            console.LogError("GetInternalParameters command with argument not integer! : " + words.at(1));
            return { UserCommand::Unknown, std::vector<int>() };
        }

        console.Log("GetInternals is for board with addr:" + std::to_string(board_addr));

        return { UserCommand::GetInternalParameters, std::vector<int>{board_addr} };
    }

  private:
    SmartLogger                     console;
    std::shared_ptr<Queue<char>>    fromUserInputQ;
    std::shared_ptr<MasterDataLink> dataLink;
};