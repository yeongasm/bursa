#pragma once
#ifndef BURSA_TYPES_HPP
#define BURSA_TYPES_HPP

#include "meta.hpp"

namespace bursa
{
static constexpr size_t npos = std::numeric_limits<size_t>::max();

struct PriceLevel
{
    size_t head = npos;    // Index of head order.
    size_t tail = npos;    // Index of tail order.
    u32 totalQty;
    u32 numOrders;

};

struct Order
{
    u32 qty;
};

struct Prices
{
    u32 min;
    u32 max;
};

struct OrderInfo
{
    order_id id;
    u32 price;
    u32 qty;
};
}

#endif // !BURSA_TYPES_HPP