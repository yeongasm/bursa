#pragma once
#ifndef ORDERBOOK_ORDERBOOK_HPP
#define ORDERBOOK_ORDERBOOK_HPP

#include <cassert>
#include <utility>
#include <algorithm>
#include <string_view>
#include <vector>
#include <limits>

#include <ankerl/unordered_dense.h>
#include <plf_colony.h>

#include "meta.hpp"

namespace bursa
{
struct Order;

struct PriceLevel
{
    Order* head;
    Order* tail;
    u32 totalQty;
    u32 orderCount;
};

struct Order
{
    u32 price;
    u32 qty;
    Order* previous;
    Order* next;
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

    // The 3 core functionalities of an orderbook is to add, remove and cancel orders.
    // These 3 functions needs to have O(1) time complexity.


    /*
    * add_order:
    * 1. An order needs to be placed inside in some container, usually in a std::vector. Although, I think using something like plf::colony would also work. Instead of having small block sizes, we can have larger block sizes to reduce the number of pages in the container.
    * 2. Since orders come with an id, we need to reference the id to the order created. We should probably use ankerl::unordered_dense::map with the order's id as the key and the order as the value. We should also reserve the capacity of the map to some reasonably large number.
    * 3. An order can be either a "bid" (buy) or an "ask" (sell). We need a vector for each of them where the element in each vector is a PriceLevel. How do we know which PriceLevel at what index to place the order in? In robust order book implementations, there is usually a floor and ceiling price that the system expect the instrument to float between. We can retrieve the PriceLevel by doing price - min_price.
    * 4. Orders are implicitly linked lists! Doing this allows us to save space by not needing another vector to store the orders inside of a PriceLevel. An order is always appended in the intrusive linked list.
    * 5. If the PriceLevel started out as empty, update the best bid / ask PriceLevel of the order book.
    */
    auto add_bid_order(OrderInfo const& info) -> void
    {
        return add_order<true>(info);
    }

    auto add_ask_order(OrderInfo const& info) -> void
    {
        return add_order<false>(info);
    }

    // A modify order is a cancel and then a re-add.
    // auto modify_order(OrderInfo const& info) -> void
    // {

    // }

    auto cancel_order(order_id id) -> void
    {
        if (!m_orderIdMap.contains(id))
        {
            return;
        }
        auto&& metadata = m_orderIdMap[id];
        auto&& order = *metadata.it;

        metadata.level->totalQty -= order.qty;

        // unlink order from price level.
        unlink_from_level(*metadata.level, *metadata.it);

        if (metadata.level->orderCount == 0)
        {
            clear_bit(metadata.idx);
        }

        m_orders.erase(metadata.it);
        m_orderIdMap.erase(id);
    }

    constexpr auto instrument_id() const -> std::string_view;

    static auto from(OrderBookCreateInfo const& info) -> OrderBook
    {
        OrderBook orderBook{ info.minPrice, info.maxPrice, info.minOrderBlockLimits, info.maxOrderBlockLimits };

        orderBook.m_orders.reserve(info.minOrderBlockLimits);
        orderBook.m_orderIdMap.reserve(info.minOrderBlockLimits);

        size_t const tickCount = static_cast<size_t>(info.maxPrice - info.minPrice) + 1ull;

        orderBook.m_bids.resize(tickCount);
        orderBook.m_asks.resize(tickCount);

        orderBook.m_bitmask.resize((tickCount / 64) + 1);

        return orderBook;
    }

private:

    using order_book_iterator = typename plf::colony<Order>::iterator;

    struct OrderMetadata
    {
        PriceLevel* level;
        size_t idx;
        order_book_iterator it;
    };

    using PriceLevels   = std::vector<PriceLevel>;
    using PriceBitMask  = std::vector<u64>;
    using OrderIdToOrderMap = ankerl::unordered_dense::map<order_id, OrderMetadata>;

    plf::colony<Order>  m_orders;
    OrderIdToOrderMap   m_orderIdMap;
    PriceLevels         m_bids;
    PriceLevels         m_asks;
    PriceBitMask        m_bitmask;
    Prices const        m_prices;

    OrderBook() = default;

    OrderBook(u32 minPrice, u32 maxPrice, u64 minBlockSize, u64 maxBlockSize) :
        m_orders{plf::limits{ minBlockSize, maxBlockSize }},
        m_orderIdMap{},
        m_bids{},
        m_asks{},
        m_bitmask{},
        m_prices{ minPrice, maxPrice }
    {}

    template <bool isBid>
    auto add_order(OrderInfo const& info) -> void
    {
        assert(info.price >= m_prices.min && info.price <= m_prices.max);
        assert(info.qty > 0);

        auto it = m_orders.emplace(info.price, info.qty, nullptr, nullptr);

        auto&& order = *it;
        // Get index of price level.
        size_t const idx = tick_index(info.price);
        auto&& level = [this](size_t i) -> PriceLevel&
        {
            if constexpr (isBid)
            {
                return m_bids[i];
            }
            else
            {
                return m_asks[i];
            }
        }(idx);

        bool const wasEmpty = level.orderCount == 0;

        link_to_level(level, order);

        // Update best bid / ask index in the order-book.
        if (wasEmpty)
        {
            set_bit(idx);
        }

        map_id_to_order<isBid>(info.id, idx, level, it);
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
        ++level.orderCount;
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

        --level.orderCount;
    }

    auto tick_index(u32 price) const -> size_t
    {
        return static_cast<size_t>(price - m_prices.min);
    }

    template <bool isBid>
    auto map_id_to_order(order_id id, size_t idx, PriceLevel& level, order_book_iterator it) -> void
    {
        if constexpr (isBid)
        {
            m_orderIdMap.emplace(id, OrderMetadata{ &level, idx, it });
        }
        else
        {
            m_orderIdMap.emplace(id, OrderMetadata{ &level, idx, it });
        }
    }

    auto set_bit(size_t idx) -> void
    {
        size_t const location = idx / 64;
        size_t const bit = idx % 64;

        if (m_bitmask[location] == 0)
        {

        }
    }

    auto clear_bit(size_t idx) -> void
    {

    }

};
}

#endif // !ORDERBOOK_ORDERBOOK_HPP