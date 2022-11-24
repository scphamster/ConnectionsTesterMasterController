#pragma once

#include <cstdint>
#include <memory>

#include "driver/gpio.h"
#include "esp_log.h"

// todo: make this class derived, and add abstract pin class
class Pin {
  public:
    using NumT     = size_t;
    using PinMaskT = uint64_t;

    enum class ActivationState {
        Active,
        Disabled
    };
    enum class Level {
        High,
        Low,
    };
    enum class Direction {
        Input,
        Output,
        InputOutput
    };
    enum class SpecialProperty {
        NoSpecials,
        OpenDrain,
        PullUp,
        PullDown,
        PullUpAndDown
    };
    enum class DriveCapability {
        Zero,
        One,
        Two,
        Three,
    };

    explicit Pin(NumT            number,
                 Direction       new_direction,
                 SpecialProperty spec_properties  = SpecialProperty::NoSpecials,
                 DriveCapability drive_capability = DriveCapability::Three) noexcept
      : pin_number{ number }
    {
        PinMaskT pinMask   = (1 << number);
        auto     pull_down = ConvertToPulldownT(spec_properties);
        auto     pull_up   = ConvertToPullupT(spec_properties);

        gpio_config_t cfg{ pinMask,
                           ConvertToGpioMode(ActivationState::Active, new_direction, spec_properties),
                           pull_up,
                           pull_down,
                           GPIO_INTR_DISABLE };

        gpio_config(&cfg);
    }
    //

    [[nodiscard]] Level GetLevel() const noexcept
    {
        auto lvl = gpio_get_level(static_cast<gpio_num_t>(pin_number));
        if (lvl == 0)
            return Level::Low;
        else
            return Level::High;
    }

  protected:
    gpio_mode_t static ConvertToGpioMode(ActivationState activation_state, Direction dir, SpecialProperty specials) noexcept
    {
        if (activation_state == ActivationState::Disabled)
            return GPIO_MODE_DISABLE;

        if (dir == Direction::Input)
            return GPIO_MODE_INPUT;

        if (specials == SpecialProperty::OpenDrain) {
            if (dir == Direction::Output)
                return GPIO_MODE_OUTPUT_OD;
            else if (dir == Direction::InputOutput)
                return GPIO_MODE_INPUT_OUTPUT_OD;

            else {
                std::terminate();
                return GPIO_MODE_DISABLE;
            }
        }
        else {
            if (dir == Direction::Output)
                return GPIO_MODE_OUTPUT;
            if (dir == Direction::InputOutput)
                return GPIO_MODE_INPUT_OUTPUT;

            else {
                std::terminate();
                return GPIO_MODE_DISABLE;
            }
        }
    }
    gpio_pullup_t static ConvertToPullupT(SpecialProperty specials) noexcept
    {
        if (specials == SpecialProperty::PullUp or specials == SpecialProperty::PullUpAndDown)
            return GPIO_PULLUP_ENABLE;
        else
            return GPIO_PULLUP_DISABLE;
    }
    gpio_pulldown_t static ConvertToPulldownT(SpecialProperty specials) noexcept
    {
        if (specials == SpecialProperty::PullDown or specials == SpecialProperty::PullUpAndDown)
            return GPIO_PULLDOWN_ENABLE;
        else
            return GPIO_PULLDOWN_DISABLE;
    }
    void static Test() noexcept
    {
        auto tag = std::string{ "PinDebugTest" };

        ESP_LOGI(tag.c_str(), "ConvertToGpioMode test begin");
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Disabled, Direction::Input, SpecialProperty::OpenDrain)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::Input, SpecialProperty::NoSpecials)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::InputOutput, SpecialProperty::NoSpecials)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::InputOutput, SpecialProperty::OpenDrain)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::Output, SpecialProperty::NoSpecials)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::Output, SpecialProperty::OpenDrain)));
        ESP_LOGI(tag.c_str(),
                 "%i",
                 static_cast<int>(ConvertToGpioMode(ActivationState::Active, Direction::Output, SpecialProperty::PullDown)));
        ESP_LOGI(tag.c_str(), "ConvertToGpioMode test end\n\n");
    }

  private:
    NumT pin_number;
};
