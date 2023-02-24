#pragma once

#include <cstdlib>
#include <array>

#include "vector_algorithms.hpp"

class MessageFromMaster {
  public:
    using Byte = uint8_t;

    union Command {
        enum class ID : Byte {
            MeasureAll = 100,
            EnableOutputForPin,
            SetOutputVoltageLevel,
            CheckConnections,
            CheckResistances,
            CheckVoltages,
            CheckRaw,
            GetBoards,
            GetInternalCounter,
            GetTaskStackWatermark,
            SetNewAddressForBoard,
            SetInternalParameters,
            GetInternalParameters,
            Test,
            DataLinkKeepAlive,
            Unknown
        };
        using Bytes = std::vector<Byte>;

        struct MeasureAll { };
        struct SetVoltageLevel {
            SetVoltageLevel(Byte byte)
            {
                if (byte > ToUnderlying(Level::High))
                    throw std::errc::invalid_argument;

                lvl = static_cast<Level>(byte);
            }
            enum class Level : Byte {
                Low = 0,
                High
            };

            Level lvl{ Level::Low };
        };
        struct SetVoltageAtPin {
            SetVoltageAtPin(const Bytes &bytes)
              : boardAffinity{ bytes.at(1) }
              , pinNumber{ bytes.at(2) }
            { }

            Byte boardAffinity;
            Byte pinNumber;
        };
        struct GetBoardsInfo { };
        struct CheckConnections {
            CheckConnections(std::vector<Byte>::const_iterator byte_it)
            {
                if (*byte_it++ == CHECK_ALL) {
                    measureAll = true;
                    return;
                }
                if (*byte_it > ADDRESSES_ALLOWED_INCLUSIVE.second or *byte_it < ADDRESSES_ALLOWED_INCLUSIVE.first) {
                    throw std::invalid_argument("board address is not inside allowed addresses : " +
                                                std::to_string(*byte_it));
                }

                boardAffinity = *byte_it++;
                if (*byte_it > MAX_PIN)
                    throw std::invalid_argument("pin number is higher than allowed: " + std::to_string(*byte_it));

                pinNumber = *byte_it;
            }
            Byte                  boardAffinity;
            Byte                  pinNumber;
            bool                  measureAll                  = false;
            constexpr static Byte CHECK_ALL                   = 255;
            constexpr static Byte MAX_PIN                     = Board::pinCount - 1;
            constexpr static auto ADDRESSES_ALLOWED_INCLUSIVE = Board::ADDRESSES_ALLOWED_INCLUSIVE;
        };
        struct KeepAliveMessage { };

        Command(std::vector<Byte> const &bytes)
        {
            auto msg_id = bytes.at(0);

            switch (static_cast<ID>(msg_id)) {
            case ID::MeasureAll: measureAll = MeasureAll(); break;
            case ID::SetOutputVoltageLevel: setVLvl = SetVoltageLevel{ bytes.at(1) }; break;
            case ID::GetBoards: getBoards = GetBoardsInfo{}; break;
            case ID::DataLinkKeepAlive: keepAlive = KeepAliveMessage{}; break;
            case ID::CheckConnections: checkConnections = CheckConnections{ bytes.cbegin() + 1 }; break;

            default: break;
            };
        }

        MeasureAll       measureAll;
        SetVoltageLevel  setVLvl;
        GetBoardsInfo    getBoards;
        KeepAliveMessage keepAlive;
        CheckConnections checkConnections;
    };

    MessageFromMaster(const std::vector<Byte> &bytes)
      : cmd{ bytes }
      , commandID{ bytes.at(0) }
    { }

    Command::ID    GetCommandID() const noexcept { return commandID; }
    [[nodiscard]] decltype(auto) GetCommand() const noexcept
    {
        switch (commandID) {
        case Command::ID::CheckConnections: {
            return cmd.checkConnections;
        }
        default: throw std::runtime_error("No implementation for cmd id: " + std::to_string(ToUnderlying(commandID)));
        }
    }

  private:
    Command     cmd;
    Command::ID commandID{};
};

class MessageToMaster {
  public:
    using Byte = uint8_t;

    virtual ~MessageToMaster() = default;

    virtual std::vector<Byte> Serialize() noexcept = 0;
};
class PinConnectivity final : MessageToMaster {
  public:
    using PinAffinityAndId= Board::PinAffinityAndId;

    struct PinConnectionData {
        PinAffinityAndId affinityAndId;
        Byte             connectionVoltageLvl;
    };

    explicit PinConnectivity(PinAffinityAndId master_pin, std::vector<PinConnectionData> &&new_connections) noexcept
      : masterPin{ std::move(master_pin) }
      , connections{ std::move(new_connections) }
    { }

    std::vector<Byte> Serialize() noexcept final
    {
        std::vector<Byte> result;
        result.reserve(sizeof(MSG_ID) + sizeof(masterPin) + connections.size() * sizeof(PinConnectionData));

        result.push_back(MSG_ID);
        result.push_back(masterPin.boardAddress);
        result.push_back(masterPin.pinId);

        for (auto &connection : connections) {
            result.push_back(connection.affinityAndId.boardAddress);
            result.push_back(connection.affinityAndId.pinId);
            result.push_back(connection.connectionVoltageLvl);
        }

        return result;
    }

  private:
    constexpr static Byte          MSG_ID = 50;
    PinAffinityAndId               masterPin;
    std::vector<PinConnectionData> connections;
};

class CommandStatus final : MessageToMaster {
  public:
    enum class Answer : Byte {
        CommandAcknowledge = 1,
        CommandNoAcknowledge,
        CommandPerformanceFailure,
        CommandPerformanceSuccess,
        CommandAcknowledgeTimeout,
        CommandPerformanceTimeout,
        CommunicationFailure,

        DeviceIsInitializing,
        KeepAliveMessage
    };

    explicit CommandStatus(Answer ans) noexcept
      : answer{ ans }
    { }

    std::vector<Byte> Serialize() noexcept final
    {
        std::vector<Byte> v;
        v.reserve(sizeof(MSG_ID) + sizeof(answer));

        v.push_back(MSG_ID);
        v.push_back(static_cast<Byte>(answer));
        return v;
    };

  private:
    constexpr static Byte MSG_ID = 51;
    Answer                answer;
};

class BoardsInfo final : MessageToMaster {
  public:
    explicit BoardsInfo(std::vector<Board::Info> &&boards_info) noexcept
      : boardsInfo{ std::move(boards_info) }
    { }

    std::vector<Byte> Serialize() noexcept final
    {
        std::vector<Byte> v;
        v.reserve(sizeof(MSG_ID) + boardsInfo.size() * ONE_BOARD_INFO_BYTES_SIZE);
        v.push_back(MSG_ID);

        for (auto const &board_info : boardsInfo) {
            const Byte *internals_as_byte_array = reinterpret_cast<const Byte *>(&board_info.internals);
            for (auto counter = 0; counter < sizeof(board_info.internals); counter++) {
                v.push_back(internals_as_byte_array[counter]);
            }

            v.push_back(board_info.address);
            v.push_back(board_info.fwVersion);
            v.push_back(ToUnderlying(board_info.voltageLevel));
            v.push_back(board_info.isHealthy);
        }

        return v;
    }

  private:
    constexpr static Byte    MSG_ID                    = 52;
    constexpr static Byte    ONE_BOARD_INFO_BYTES_SIZE = sizeof(Board::Info);
    std::vector<Board::Info> boardsInfo;
};

class AllBoardsVoltages final : MessageToMaster {
  public:
    explicit AllBoardsVoltages(std::vector<Board::OneBoardVoltages> &&boards_voltages) noexcept
      : boardsVoltages{ std::move(boards_voltages) }
    { }

    void AppendBoardVoltages(Board::OneBoardVoltages &&board_voltages) noexcept
    {
        auto same_id_board =
          std::find_if(boardsVoltages.begin(), boardsVoltages.end(), [&board_voltages](auto const &boardVoltages) {
              return boardVoltages.boardAddress == board_voltages.boardAddress;
          });

        if (same_id_board != boardsVoltages.end()) {
            same_id_board->pinsVoltages = board_voltages.pinsVoltages;
        }
        else {
            boardsVoltages.emplace_back(std::move(board_voltages));
        }
    }

    std::vector<Byte> Serialize() noexcept override
    {
        std::vector<Byte> v;
        v.reserve(ONE_BOARD_VOLTAGES_SIZE_BYTES * boardsVoltages.size());
        v.push_back(MSG_ID);

        for (auto const &boardVoltages : boardsVoltages) {
            v.push_back(boardVoltages.boardAddress);

            int counter = 0;
            for (auto const &voltage : boardVoltages.pinsVoltages) {
                v.push_back(Board::logicPinToPinOnBoardMapping.at(counter));
                v.push_back(voltage);

                counter++;
            }
        }

        return v;
    }

  private:
    constexpr static Byte                MSG_ID                        = 53;
    constexpr static auto                ONE_BOARD_VOLTAGES_SIZE_BYTES = 1 + Board::pinCount * 2;
    std::vector<Board::OneBoardVoltages> boardsVoltages;
};

class Status final : MessageToMaster {
  public:
    enum class StatusValue : Byte {
        Initializing = 0,
        Operating,
    };

    enum class WiFiStatus : Byte {
        Disabled,
        Connecting,
        Connected,
        FailedAuth,
        UnknownFail
    };

    enum class BTStatus : Byte {
        Disabled,
        Connecting,
        Connected,
        NoConnection,
        UnknownFail
    };

    enum class BoardsStatus : Byte {
        Searching,
        NoBoardsFound,
        OldFirmwareBoardFound,
        AtLeastOneBoardFound,
        VeryUnhealthyConnection   // if controller reaches failed messages count this flag is set
    };

    std::vector<Byte> Serialize() noexcept final
    {
        return std::vector{ ToUnderlying(status), ToUnderlying(wifi), ToUnderlying(bt), ToUnderlying(boards) };
    }

  private:
    auto constexpr static BYTES_SIZE = 4;
    //    constexpr static Byte MSG_ID     = 53;
    StatusValue  status{ StatusValue::Initializing };
    WiFiStatus   wifi{};
    BTStatus     bt{ BTStatus::Disabled };
    BoardsStatus boards{ BoardsStatus::Searching };
};
