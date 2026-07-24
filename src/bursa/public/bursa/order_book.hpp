#pragma once
#ifndef BURSA_ORDER_BOOK_HPP
#define BURSA_ORDER_BOOK_HPP

#include <cassert>
#include <string_view>
#include <vector>

#include <ankerl/unordered_dense.h>

#include "order_pool.hpp"
#include "tick_bitset.hpp"

namespace bursa
{
template <typename Environment>
class OrderBook : public non_copyable
{
public:
    using instrument_type = instrument_of_t<Environment>;

    auto best_bid() const -> std::pair<u64, PriceLevel const*>
    {
        auto const idx = bidsBitset.highest();
        if (idx == PriceLevelBitset::npos)
        {
            return {};
        }
        return { static_cast<u64>(prices.min + idx), &bids[idx] };
    }

    auto best_ask() const -> std::pair<u64, PriceLevel const*>
    {
        auto const idx = asksBitset.lowest();
        if (idx == PriceLevelBitset::npos)
        {
            return {};
        }
        return { static_cast<u64>(prices.min + idx), &asks[idx] };
    }

    auto add_bid_order(OrderInfo const& info) -> void
    {
        return add_order(info, bids, bidsBitset);
    }

    auto add_ask_order(OrderInfo const& info) -> void
    {
        return add_order(info, asks, asksBitset);
    }

    auto modify_order(OrderInfo const& info) -> void
    {
        // Undecided on how to handle prices outside of the min & max range.
        // We could clamp but theoritically it's incorrect. There is a few options I can think of:
        // 1. Fallback to a map but it will be slow.
        // 2. Don't let our order book handle this edge case. Instead, the client will be responsible in deciding which limit order book should the order be pushed to.

        assert(info.price >= prices.min && info.price <= prices.max);
        assert(info.qty > 0);

        auto it = orderMetadatas.find(info.id);

        if (it == orderMetadatas.end())
        {
            return;
        }

        auto& metadata = it->second;
        auto& node = orders.at(metadata.orderIdx);

        size_t const lvlIdx = tick_index(info.price);

        // On virtually every price-time-priority venue, a pure size-down **retains** queue position; only a price change or a size *increase* loses it
        if (lvlIdx == metadata.levelIdx && info.qty <= node.order.qty)
        {
            metadata.level().totalQty -= (node.order.qty - info.qty);
            node.order.qty = info.qty;
            return;
        }

        auto levels = metadata.levels;
        auto bitset = metadata.bitset;

        cancel_order(it);
        add_order(info, *levels, *bitset);
    }

    auto cancel_order(order_id id) -> void
    {
        auto it = orderMetadatas.find(id);

        if (it == orderMetadatas.end())
        {
            return;
        }

        cancel_order(it);
    }

    auto execute_order(order_id id, u32 quantity) -> void
    {
        auto it = orderMetadatas.find(id);

        if (it == orderMetadatas.end())
        {
            return;
        }

        auto& metadata = it->second;
        auto& node = orders.at(metadata.orderIdx);
        auto& level = metadata.level();

        quantity = std::min(quantity, node.order.qty);

        level.totalQty -= quantity;
        node.order.qty -= quantity;

        if (node.order.qty == 0)
        {
            remove_order(it);
        }
    }

    constexpr auto instrument_id() const -> std::string_view
    {
        return instrument_id_v<instrument_type>;
    }

    static auto from(Prices const& prices, std::pmr::polymorphic_allocator<std::byte> const& allocator = std::pmr::polymorphic_allocator<std::byte>{}) -> OrderBook
    {
        OrderBook orderBook{ prices, allocator.resource() };

        size_t const tickCount = static_cast<size_t>(prices.max - prices.min) + 1ull;

        orderBook.bids.resize(tickCount);
        orderBook.asks.resize(tickCount);

        orderBook.bidsBitset.resize(tickCount);
        orderBook.asksBitset.resize(tickCount);

        orderBook.orderMetadatas.reserve(524'288);

        return orderBook;
    }

private:
    using PriceLevelBitset      = TickBitset<u64>;
    using PriceLevels           = std::vector<PriceLevel, std::pmr::polymorphic_allocator<PriceLevel>>;

    struct OrderMetadata
    {
        PriceLevels* levels;
        PriceLevelBitset* bitset;
        size_t levelIdx;
        size_t orderIdx;

        auto level() -> PriceLevel&
        {
            return (*levels)[levelIdx];
        }
    };

    using OrderMetadataMap = ankerl::unordered_dense::map<order_id, OrderMetadata>;
    using order_metadata_iterator = OrderMetadataMap::iterator;

    OrderPool           orders;
    OrderMetadataMap    orderMetadatas;
    PriceLevels         bids;
    PriceLevelBitset    bidsBitset;
    PriceLevels         asks;
    PriceLevelBitset    asksBitset;
    Prices              prices;

    OrderBook(Prices const& prices, std::pmr::polymorphic_allocator<std::byte> const& allocator) :
        orders{ 524'288, allocator.resource() },
        orderMetadatas{},
        bids(allocator.resource()),
        bidsBitset{ allocator.resource() },
        asks(allocator.resource()),
        asksBitset{ allocator.resource() },
        prices{ prices }
    {}

    auto add_order(OrderInfo const& info, PriceLevels& priceLevels, PriceLevelBitset& bitset) -> void
    {
        assert(info.price >= prices.min && info.price <= prices.max);
        assert(info.qty > 0);

        if (orderMetadatas.contains(info.id))
        {
            return;
        }

        size_t const orderIdx = orders.emplace(info.qty);
        auto&& node = orders.at(orderIdx);
        // Get index of price level.
        size_t const levelIdx = tick_index(info.price);
        auto&& level = priceLevels[levelIdx];

        link_to_level(level, orderIdx);

        level.tail = orderIdx;
        level.totalQty += node.order.qty;

        add_order_metadata(info.id, levelIdx, bitset, priceLevels, orderIdx);

        bitset.set(levelIdx);
    }

    auto cancel_order(order_metadata_iterator it) -> void
    {
        auto&& metadata = it->second;
        auto& level = metadata.level();
        auto& node = orders.at(metadata.orderIdx);

        level.totalQty -= node.order.qty;

        remove_order(it);
    }

    auto remove_order(order_metadata_iterator it) -> void
    {
        auto& meta = it->second;
        auto& level = meta.level();

        unlink_from_level(level, meta.orderIdx);

        if (level.numOrders == 0)
        {
            meta.bitset->clear(meta.levelIdx);
        }

        orders.erase(meta.orderIdx);
        orderMetadatas.erase(it);
    }

    auto link_to_level(PriceLevel& level, size_t orderIdx) -> void
    {
        auto& node = orders.at(orderIdx);

        node.previous = level.tail;

        if (level.tail != npos)
        {
            orders.at(level.tail).next = orderIdx;
        }
        else
        {
            level.head = orderIdx;
        }

        ++level.numOrders;
    }

    auto unlink_from_level(PriceLevel& level, size_t orderIdx) -> void
    {
        auto& node = orders.at(orderIdx);

        if (node.previous != npos)
        {
            orders.at(node.previous).next = node.next;
        }
        else
        {
            level.head = node.next;
        }

        if (node.next != npos)
        {
            orders.at(node.next).previous = node.previous;
        }
        else
        {
            level.tail = node.previous;
        }

        --level.numOrders;
    }

    auto tick_index(u32 price) const -> size_t
    {
        return static_cast<size_t>(price - prices.min);
    }

    auto add_order_metadata(order_id id, size_t levelIdx, PriceLevelBitset& bitset, PriceLevels& levels, size_t orderIdx) -> void
    {
        orderMetadatas.emplace(id, OrderMetadata{ &levels, &bitset, levelIdx, orderIdx });
    }
};
}

#endif // !BURSA_ORDER_BOOK_HPP