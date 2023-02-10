#pragma once

#include <cstdlib>
#include <array>

#include "vector_algorithms.hpp"

struct MessageFromMaster {
    using Byte = uint8_t;

    enum class MessageType : Byte {
        COMMAND = 200,
        RESULTS,
    };

    union Data {
        struct Command {
            enum class UserCommand {
                MeasureAll = 100,
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

            Command() = default;
            Command(std::vector<Byte> byte_array)
            {
                auto iterator = byte_array.begin();

                auto cmdID = VectorAlgorithm::Make<int>(iterator, byte_array.end());

                if (cmdID == std::nullopt) {
                    throw std::length_error("make failed!");
                }

                commandID = *cmdID;

                switch (commandID) {
                case ToUnderlying(UserCommand::MeasureAll): break;
                default: break;
                }
            }

            struct MeasureAll {
                constexpr static auto CMD_ARGS_LEN_BYTES = 0;
            };

            int              commandID = -1;
            std::vector<int> arguments{};
        };
        struct MeasurementsResult {
            using BoardId                                             = Byte;
            using VoltageOnPin                                        = Byte;
            constexpr static int                      SIZE_BYTES      = Board::pinCount + 1;
            BoardId                                   resultsForBoard = 0;
            std::array<VoltageOnPin, Board::pinCount> pinsVoltages{};
        };
    };

    MessageType intrinsicDataType{};
    Data        data;
};

class MessageToMaster {
  public:
    using Byte = uint8_t;

    virtual ~MessageToMaster() = default;

    virtual std::vector<Byte> Serialize() noexcept = 0;
};

class PinConnectivity final : MessageToMaster {
  public:
    struct PinAffinityAndId {
        Byte boardAffinity;
        Byte id;


    };
    struct PinConnectionData {
        PinAffinityAndId affinityAndId;
        Byte             connectionVoltageLvl;
    };

    explicit PinConnectivity(PinAffinityAndId master_pin, std::vector<PinConnectionData> new_connections) noexcept
      : masterPin{ std::move(master_pin) }
      , connections{ std::move(new_connections) }
    { }

    std::vector<Byte> Serialize() noexcept final
    {
        std::vector<Byte> result;
        result.reserve(sizeof(MSG_ID) + sizeof(masterPin) + 1 + connections.size() * sizeof(PinConnectionData));

        result.push_back(MSG_ID);
        result.push_back(masterPin.boardAffinity);
        result.push_back(masterPin.id);
        result.push_back(static_cast<Byte>(connections.size()));

        for (auto &connection : connections) {
            result.push_back(connection.affinityAndId.boardAffinity);
            result.push_back(connection.affinityAndId.id);
            result.push_back(connection.connectionVoltageLvl);
        }

        return result;
    }

  private:
    constexpr static Byte          MSG_ID = 50;
    PinAffinityAndId               masterPin;
    std::vector<PinConnectionData> connections;
};

class StatusInfo {
    struct BoardStatus {
        Board::AddressT         address;
        Board::InternalCounterT internalCounter;
        Board::FirmwareVersionT firmwareVersion;
    };

    std::vector<Board::AddressT> boardsOnLine;
};
