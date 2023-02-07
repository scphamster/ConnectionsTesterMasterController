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
    Data     data;
};

struct MessageToMaster {

};