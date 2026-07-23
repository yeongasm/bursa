#pragma once
#ifndef BURSA_ORDER_BOOK_HPP
#define BURSA_ORDER_BOOK_HPP

#include <cassert>
#include <string_view>
#include <vector>
#include <limits>

#include <ankerl/unordered_dense.h>
#include <plf_colony.h>

#include "tick_bitset.hpp"

namespace bursa
{
struct Order;

struct PriceLevel
{
    Order* head;
    Order* tail;
    u32 totalQty;
    u32 numOrders;
};

struct Order
{
    u32 qty;
    Order* previous;
    Order* next;
};

struct Prices
{
    u64 min;
    u64 max;
};

struct OrderInfo
{
    order_id id;
    u32 price;
    u32 qty;
};

struct OrderBookCreateInfo
{
    u32 minPrice;
    u32 maxPrice;
    u64 minOrderBlockLimits = static_cast<u64>(std::numeric_limits<u16>::max());
    u64 maxOrderBlockLimits = static_cast<u64>(std::numeric_limits<u16>::max());
};

template <typename Environment>
class OrderBook : public non_copyable
{
public:

    using instrument_type = instrument_of_t<Environment>;

    auto best_bid() const -> std::pair<u64, PriceLevel const*>
    {
        auto const idx = m_bidsBitset.highest();
        if (idx == PriceLevelBitset::npos)
        {
            return {};
        }
        return { static_cast<u64>(m_prices.min + idx), &m_bids[idx] };
    }

    auto best_ask() const -> std::pair<u64, PriceLevel const*>
    {
        auto const idx = m_asksBitset.lowest();
        if (idx == PriceLevelBitset::npos)
        {
            return {};
        }
        return { static_cast<u64>(m_prices.min + idx), &m_asks[idx] };
    }

    auto add_bid_order(OrderInfo const& info) -> void
    {
        return add_order(info, m_bids, m_bidsBitset);
    }

    auto add_ask_order(OrderInfo const& info) -> void
    {
        return add_order(info, m_asks, m_asksBitset);
    }

    auto modify_order(OrderInfo const& info) -> void
    {
        // Undecided on how to handle prices outside of the min & max range.
        // We could clamp but theoritically it's incorrect. There is a few options I can think of:
        // 1. Fallback to a map but it will be slow.
        // 2. Don't let our order book handle this edge case. Instead, the client will be responsible in deciding which limit order book should the order be pushed to.

        assert(info.price >= m_prices.min && info.price <= m_prices.max);
        assert(info.qty > 0);

        auto it = m_orderIdMap.find(info.id);
        if (it == m_orderIdMap.end())
        {
            return;
        }

        OrderMetadata& metadata = it->second;
        Order& order = *metadata.it;

        size_t const priceIndex = tick_index(info.price);

        // On virtually every price-time-priority venue, a pure size-down **retains** queue position; only a price change or a size *increase* loses it
        if (priceIndex == metadata.idx && info.qty <= order.qty)
        {
            metadata.level().totalQty -= (order.qty - info.qty);
            order.qty = info.qty;
            return;
        }

        auto levels  = metadata.levels;
        auto bitset = metadata.bitset;

        cancel_order(it);
        add_order(info, *levels, *bitset);
    }

    auto cancel_order(order_id id) -> void
    {
        auto it = m_orderIdMap.find(id);
        if (it == m_orderIdMap.end())
        {
            return;
        }
        cancel_order(it);
    }

    // auto execute_order() -> void
    // {

    // }

    // auto print_order_book() const -> void
    // {

    // }

    constexpr auto instrument_id() const -> std::string_view
    {
        return instrument_id_v<instrument_type>;
    }

    static auto from(OrderBookCreateInfo const& info) -> OrderBook
    {
        OrderBook orderBook{ info.minPrice, info.maxPrice, info.minOrderBlockLimits, info.maxOrderBlockLimits };

        orderBook.m_orders.reserve(info.minOrderBlockLimits);
        orderBook.m_orderIdMap.reserve(info.minOrderBlockLimits);

        size_t const tickCount = static_cast<size_t>(info.maxPrice - info.minPrice) + 1ull;

        orderBook.m_bids.resize(tickCount);
        orderBook.m_asks.resize(tickCount);

        orderBook.m_bidsBitset.resize(tickCount);
        orderBook.m_asksBitset.resize(tickCount);

        return orderBook;
    }

private:

    using order_book_iterator   = typename plf::colony<Order>::iterator;
    using PriceLevelBitset      = TickBitset<u64>;
    using PriceLevels           = std::vector<PriceLevel>;

    struct OrderMetadata
    {
        PriceLevels* levels;
        PriceLevelBitset* bitset;
        size_t idx;
        order_book_iterator it;

        auto level() -> PriceLevel&
        {
            return (*levels)[idx];
        }
    };


    using OrderIdToOrderMap = ankerl::unordered_dense::map<order_id, OrderMetadata>;
    using order_metadata_iterator = OrderIdToOrderMap::iterator;

    plf::colony<Order>  m_orders;
    OrderIdToOrderMap   m_orderIdMap;
    PriceLevels         m_bids;
    PriceLevelBitset    m_bidsBitset;
    PriceLevels         m_asks;
    PriceLevelBitset    m_asksBitset;
    Prices              m_prices;

    OrderBook(u32 minPrice, u32 maxPrice, u64 minBlockSize, u64 maxBlockSize) :
        m_orders{plf::limits{ minBlockSize, maxBlockSize }},
        m_orderIdMap{},
        m_bids{},
        m_asks{},
        m_prices{ minPrice, maxPrice }
    {}

    auto add_order(OrderInfo const& info, PriceLevels& priceLevels, PriceLevelBitset& bitset) -> void
    {
        assert(info.price >= m_prices.min && info.price <= m_prices.max);
        assert(info.qty > 0);

        if (m_orderIdMap.contains(info.id))
        {
            return;
        }

        auto it = m_orders.emplace(info.qty, nullptr, nullptr);

        auto&& order = *it;
        // Get index of price level.
        size_t const idx = tick_index(info.price);
        auto&& level = priceLevels[idx];

        link_to_level(level, order);

        map_id_to_order(info.id, idx, bitset, priceLevels, it);

        bitset.set(idx);
    }

    auto cancel_order(order_metadata_iterator it) -> void
    {
        auto&& metadata = it->second;

        // unlink order from price level.
        unlink_from_level(metadata.level(), *metadata.it);

        if (metadata.level().numOrders == 0)
        {
            metadata.bitset->clear(metadata.idx);
        }

        m_orders.erase(metadata.it);
        m_orderIdMap.erase(it);
    }

    auto link_to_level(PriceLevel& level, Order& order) -> void
    {
        order.previous = level.tail;

        if (level.tail != nullptr)
        {
            level.tail->next = &order;
        }
        else
        {
            level.head = &order;
        }

        level.tail = &order;
        level.totalQty += order.qty;
        ++level.numOrders;
    }

    auto unlink_from_level(PriceLevel& level, Order& order) -> void
    {
        if (order.previous != nullptr)
        {
            order.previous->next = order.next;
        }
        else
        {
            level.head = order.next;
        }

        if (order.next != nullptr)
        {
            order.next->previous = order.previous;
        }
        else
        {
            level.tail = order.previous;
        }

        level.totalQty -= order.qty;
        --level.numOrders;
    }

    auto tick_index(u32 price) const -> size_t
    {
        return static_cast<size_t>(price - m_prices.min);
    }

    auto map_id_to_order(order_id id, size_t idx, PriceLevelBitset& bitset, PriceLevels& levels, order_book_iterator it) -> void
    {
        m_orderIdMap.emplace(id, OrderMetadata{ &levels, &bitset, idx, it });
    }
};
}

#endif // !BURSA_ORDER_BOOK_HPP