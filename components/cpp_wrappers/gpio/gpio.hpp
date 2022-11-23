#pragma once

#include "driver/gpio.h"
#include <memory>
#include <cstdint>

class Pin {
  public:
    using NumT = size_t;

    enum class state : bool {
        High = true,
        Low  = false
    };
    enum class Direction : bool {
        Input  = false,
        Output = true
    };
    enum class SpecialProperty {
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

    explicit Pin(NumT number) noexcept { ; }

  private:
    NumT pin_number;

};

class IO {
  public:
    void somefunc() { }

  private:
};