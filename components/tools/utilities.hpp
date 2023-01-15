#pragma once
#include <type_traits>

constexpr auto ToUnderlying(auto argument) {
    return static_cast<std::underlying_type_t<decltype(argument)>>(argument);
}