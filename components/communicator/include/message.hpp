#pragma once

#include <cstdlib>
#include <array>

struct Message {
    using Byte = uint8_t;
    std::array<Byte, 5> test {1,2,3,4,5};
    short someshort = 12345;
};