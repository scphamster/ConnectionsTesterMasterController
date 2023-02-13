#pragma once

#include <vector>

class VectorAlgo {
  public:
    using Byte       = uint8_t;
    using IteratorT  = std::vector<Byte>::iterator;
    using CIteratorT = std::vector<Byte>::const_iterator;
    template<typename ReturnType>
    static std::optional<ReturnType> Make(IteratorT &iterator, const CIteratorT &end) noexcept
    {
        if ((sizeof(ReturnType) > (end - iterator)) or (iterator >= end))
            return std::nullopt;

        std::array<Byte, sizeof(ReturnType)> buffer;

        for (auto &byte : buffer) {
            byte = *iterator;
            iterator++;
        }

        return *reinterpret_cast<ReturnType *>(buffer.data());
    }
};