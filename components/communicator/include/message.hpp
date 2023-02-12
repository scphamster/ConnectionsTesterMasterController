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
            GetAllBoardsIds,
            GetInternalCounter,
            GetTaskStackWatermark,
            SetNewAddressForBoard,
            SetInternalParameters,
            GetInternalParameters,
            Test,
            Unknown
        };
        using Bytes = std::vector<Byte>;
        struct MeasureAll { };
        struct SetVoltageAtPin {
            SetVoltageAtPin(const Bytes &bytes)
              : boardAffinity{ bytes.at(1) }
              , pinNumber{ bytes.at(2) }
            { }

            Byte boardAffinity;
            Byte pinNumber;
        };
        Command(std::vector<Byte> const &bytes)
        {
            auto msg_id = bytes.at(0);

            switch (static_cast<ID>(msg_id)) {
            case ID::MeasureAll: measureAll = MeasureAll(); break;

            default: break;
            };
        }

        MeasureAll measureAll;
    };

    MessageFromMaster(const std::vector<Byte> &bytes)
      : cmd{ bytes }
      , commandID{ bytes.at(0) }
    { }

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
