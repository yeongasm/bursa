#pragma once
#ifndef SANDBOX_DATASET_HPP
#define SANDBOX_DATASET_HPP

#include <vector>
#include <algorithm>
#include <ranges>
#include <limits>

#include "bursa/types.hpp"

namespace test
{
namespace details
{
constexpr auto next_random(u32& state) -> u32
{
    state = state * 1'664'525u + 1'013'904'223u;
    return state;
};

constexpr auto mean_distribution_price(u32 minPrice, u32 maxPrice, u32 priceSpan, u32 middlePrice, u32& state) -> u32
{
    constexpr auto RANDOM_MAX = static_cast<u64>(std::numeric_limits<u32>::max());

    u64 sum = {};

    for ([[maybe_unused]] auto i : std::views::iota(0, 12))
    {
        sum += static_cast<u64>(next_random(state));
    }

    auto const meanSum = 6ull * RANDOM_MAX;
    auto const centeredSum = static_cast<i64>(sum) - static_cast<i64>(meanSum);

    // Maps approximately +/- six standard deviations to the
    // configured price range.
    auto const priceOffset =
        centeredSum * static_cast<i64>(priceSpan) /
        static_cast<i64>(12ull * RANDOM_MAX);

    auto price = middlePrice + priceOffset;

    price = std::clamp(price, static_cast<i64>(minPrice), static_cast<i64>(maxPrice));

    return static_cast<u32>(price);
};
}

enum class Message : u8
{
    Add_Bid,
    Add_Ask,
    Cancel,
    Execute,
    Modify
};

struct MakeDatasetInfo
{
    u32 minPrice;
    u32 maxPrice;
    u32 minQuantity;
    u32 maxQuantity;
    size_t datasetSize;
};

inline auto make_dataset(MakeDatasetInfo const& info) -> std::vector<bursa::OrderInfo>
{
    std::vector<bursa::OrderInfo> output;

    output.reserve(info.datasetSize);

    u32 state = 0x12345678u;

    auto const priceSpan = info.maxPrice - info.minPrice;
    auto const middlePrice = info.minPrice + priceSpan / 2;
    auto const quantitySpan = info.maxQuantity - info.minQuantity + 1u;

    for (size_t i = 0; i < info.datasetSize; ++i)
    {
        auto price = details::mean_distribution_price(
            info.minPrice,
            info.maxPrice,
            priceSpan,
            middlePrice,
            state
        );

        auto quantity = info.minQuantity + details::next_random(state) % quantitySpan;

        output.emplace_back(static_cast<bursa::order_id>(i + 1u), price, quantity);
    }

    return output;
};
}

#endif // !SANDBOX_DATASET_HPP