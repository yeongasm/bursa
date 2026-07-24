#pragma once
#ifndef BURSA_TICK_BITSET_HPP
#define BURSA_TICK_BITSET_HPP

#include <concepts>
#include <bit>
#include <utility>
#include <vector>

#include "meta.hpp"

namespace bursa
{
template <std::integral T>
class TickBitset : public non_copyable
{
public:
    using value_type = T;

    static constexpr size_t BITS_PER_WORD   = sizeof(value_type) * CHAR_BIT;
    static constexpr size_t npos            = std::numeric_limits<size_t>::max();

    TickBitset(std::pmr::polymorphic_allocator<value_type> const& allocator) :
        leafs(allocator),
        roots(allocator)
    {}

    auto resize(size_t tickCount) -> void
    {
        auto const leafWords = word_count(tickCount);
        auto const rootWords = word_count(leafWords);

        leafs.resize(leafWords);
        roots.resize(rootWords);
    };

    auto set(size_t idx) -> void
    {
        auto const [leafIndex, leafBit] = split_index(idx);

        if (leafs[leafIndex] == 0)
        {
            auto const [rootIndex, rootBit] = split_index(leafIndex);
            roots[rootIndex] |= value_type{ 1 } << rootBit;
        }

        leafs[leafIndex] |= value_type{ 1 } << leafBit;
    };

    auto clear(size_t idx) -> void
    {
        auto const [leafIndex, leafBit] = split_index(idx);

        leafs[leafIndex] &= ~(value_type{ 1 } << leafBit);

        if (leafs[leafIndex] == 0)
        {
            auto const [rootIndex, rootBit] = split_index(leafIndex);
            roots[rootIndex] &= ~(value_type{ 1 } << rootBit);
        }
    };

    auto highest() const -> size_t
    {
        for (size_t i = roots.size(); i-- > 0;)
        {
            if (roots[i] != 0)
            {
                size_t const leafIdx = i * BITS_PER_WORD + highest_bit(roots[i]);
                return leafIdx * BITS_PER_WORD + highest_bit(leafs[leafIdx]);
            }
        }
        return npos;
    }

    auto lowest() const -> size_t
    {
        for (size_t i = {}; i < roots.size(); ++i)
        {
            if (roots[i] != 0)
            {
                size_t const leafIdx = i * BITS_PER_WORD + lowest_bit(roots[i]);
                return leafIdx * BITS_PER_WORD + lowest_bit(leafs[leafIdx]);
            }
        }
        return npos;
    }

private:
    using container_type = std::vector<value_type, std::pmr::polymorphic_allocator<value_type>>;

    container_type leafs;
    container_type roots;

    auto word_count(size_t bits) const -> size_t
    {
        return (bits + BITS_PER_WORD - 1) / BITS_PER_WORD;
    }

    /*
    * This gives us the index that we should access and the specific bit we should be setting.
    */
    auto split_index(size_t idx) const -> std::pair<size_t, size_t>
    {
        return { idx / BITS_PER_WORD, idx % BITS_PER_WORD };
    }

    auto highest_bit(value_type num) const -> size_t
    {
        return (BITS_PER_WORD - 1) - static_cast<value_type>(std::countl_zero(num));
    }

    auto lowest_bit(value_type num) const -> size_t
    {
        return static_cast<value_type>(std::countr_zero(num));
    }
};
}

#endif // !BURSA_TICK_BITSET_HPP