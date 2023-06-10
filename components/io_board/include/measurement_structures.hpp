#pragma once

class AllBoardsVoltages {
  public:
    using Byte          = uint8_t;
    using OneBoardVoltages = Board::OneBoardVoltages;
    AllBoardsVoltages() = default;
    explicit AllBoardsVoltages(std::vector<OneBoardVoltages> voltages)
      : results{ std::move(voltages) }
    { }

    std::vector<Byte> Serialize()
    {
        constexpr auto one_board_results_size =
          sizeof(OneBoardVoltages::boardAddress) + sizeof(Board::AdcValueLoRes) * Board::pinCount;

        std::vector<Byte> bytes;
        bytes.reserve(one_board_results_size * results.size());
        auto byte_iterator = bytes.begin();

        for (auto const &result : results) {
            *byte_iterator++ = result.boardAddress;

            for (auto const pin_voltage : result.pinsVoltages) {
                *byte_iterator++ = pin_voltage;
            }
        }
    }

  private:
    std::vector<OneBoardVoltages> results;
};