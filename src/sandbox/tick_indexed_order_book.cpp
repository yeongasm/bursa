// tick_indexed_order_book.cpp
//
// Educational example of the "third variant" order book design mentioned
// alongside the two GitHub repos we compared: a dense, tick-indexed array
// of price levels, where each level holds an INTRUSIVE doubly linked list
// of orders (so time priority / FIFO is preserved within a price), and
// order ids are resolved through a flat, pooled table rather than a
// std::map or std::unordered_map.
//
// This is deliberately built from the pieces both reference repos used
// separately:
//   - charles-cooper/itch-order-book gave us: no per-node heap allocation,
//     a pooled/handle-based order table, arrays over trees for locality.
//   - Hemmy123/exchange-demo gave us: a real per-price FIFO queue of
//     individual orders, not just an aggregate quantity.
//   - What neither repo did: address price levels *directly* by tick
//     (O(1), no search at all) instead of a sorted-and-searched array
//     or a std::map. That direct addressing is only possible because we
//     fix a bounded, known tick range up front -- which is exactly the
//     assumption real venues let you make for a single instrument with
//     a fixed tick size and known price band.
//
// Design summary
// ---------------
//   Price     -> tick-relative integer, NOT a float/double (avoids FP
//                comparison bugs and lets us index arrays directly).
//   Level     -> { head, tail, total_qty, order_count }. No price field
//                needed: the level's price *is* its array index.
//   Order     -> lives in a flat pool (std::vector<Order>), never on the
//                heap individually. Carries intrusive prev/next indices
//                so the doubly linked list costs zero extra allocations.
//   id_to_slot_ -> order id -> pool slot, as a dense vector indexed by id.
//                This assumes ids are small, non-negative integers
//                (true for many exchange feeds, e.g. ITCH order
//                references within a trading day). If your venue hands
//                out sparse or very large ids, swap this one vector for
//                a std::unordered_map<OrderId, int32_t> and everything
//                else in this file stays the same.
//
// Complexity (N = number of distinct ticks in the configured band,
//             M = number of *active* price levels, k = orders at a level)
//   add_order       : O(1) amortized  (level found by direct index;
//                      list push_back; occasional order_pool_ growth)
//   cancel_order     : O(1)            (intrusive unlink, no search)
//   execute_order    : O(1)            (same as cancel, partial or full)
//   best_bid/ask     : O(1) amortized, O(distance to next active level)
//                      worst case -- see update_best_after_removal().
//                      This worst case is the same "walk near the touch"
//                      trade-off the sorted-vector design had; the
//                      difference is we only pay it when the *best*
//                      level itself empties out, not on every insert.
//
// Memory cost: O(N) fixed, regardless of how many levels are actually
// active -- this is the trade you make for direct addressing. Fine for
// a single equity/future with a narrow, fixed tick grid; wasteful for
// something like FX or an options chain with a wide or unbounded range,
// where the std::map-based design from the previous message is the
// better fit.

#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <vector>

using OrderId = uint32_t;
using Price   = int64_t;  // price expressed in ticks, e.g. cents
using Qty     = int64_t;

constexpr int32_t kNullSlot = -1;

struct Order {
    OrderId id     = 0;
    Price   price  = 0;
    Qty     qty    = 0;
    bool    is_bid = false;

    // Intrusive doubly linked list pointers -- indices into the order
    // pool, not raw pointers. Indices stay valid even if the pool's
    // backing std::vector reallocates, which raw pointers would not.
    int32_t prev = kNullSlot;
    int32_t next = kNullSlot;
};

struct Level {
    int32_t head        = kNullSlot;
    int32_t tail        = kNullSlot;
    Qty     total_qty   = 0;
    uint32_t order_count = 0;

    bool empty() const { return head == kNullSlot; }
};

class TickIndexedOrderBook {
public:
    // [min_price, max_price] inclusive, in ticks. Both sides of the book
    // share the same band here for simplicity; real books often size
    // the array around the instrument's expected trading range with
    // some headroom (e.g. +/- 20% from the opening price).
    TickIndexedOrderBook(Price min_price, Price max_price)
        : min_price_(min_price),
          max_price_(max_price),
          bids_(static_cast<size_t>(max_price - min_price + 1)),
          asks_(static_cast<size_t>(max_price - min_price + 1)) {
        assert(max_price >= min_price);
    }

    void add_order(OrderId id, bool is_bid, Price price, Qty qty) {
        assert(price >= min_price_ && price <= max_price_);
        assert(qty > 0);

        int32_t slot = alloc_slot();
        Order& o = order_pool_[static_cast<size_t>(slot)];
        o.id = id;
        o.price = price;
        o.qty = qty;
        o.is_bid = is_bid;
        o.prev = o.next = kNullSlot;

        register_id(id, slot);

        size_t idx = tick_index(price);
        Level& lvl = is_bid ? bids_[idx] : asks_[idx];
        bool was_empty = lvl.empty();

        link_to_level(lvl, slot);
        lvl.total_qty += qty;

        if (was_empty) update_best_after_insert(is_bid, idx);
    }

    // Full cancel: remove the order entirely regardless of remaining qty.
    void cancel_order(OrderId id) {
        int32_t slot = lookup_slot(id);
        if (slot == kNullSlot) return;  // unknown / already removed

        Order& o = order_pool_[static_cast<size_t>(slot)];
        size_t idx = tick_index(o.price);
        Level& lvl = o.is_bid ? bids_[idx] : asks_[idx];

        lvl.total_qty -= o.qty;
        unlink_from_level(lvl, slot);
        bool now_empty = lvl.empty();

        unregister_id(id);
        free_slot(slot);

        if (now_empty) update_best_after_removal(o.is_bid, idx);
    }

    // Partial or full execution. A full execution (exec_qty == remaining
    // qty) removes the order; a partial one just reduces its quantity
    // and -- correctly -- keeps it at its current queue position, since
    // ITCH-style price-time priority does not re-queue an order for a
    // partial fill, only for a price/qty-increasing replace.
    void execute_order(OrderId id, Qty exec_qty) {
        int32_t slot = lookup_slot(id);
        if (slot == kNullSlot) return;

        Order& o = order_pool_[static_cast<size_t>(slot)];
        assert(exec_qty > 0 && exec_qty <= o.qty);

        size_t idx = tick_index(o.price);
        Level& lvl = o.is_bid ? bids_[idx] : asks_[idx];

        lvl.total_qty -= exec_qty;
        o.qty -= exec_qty;

        if (o.qty == 0) {
            unlink_from_level(lvl, slot);
            bool now_empty = lvl.empty();
            unregister_id(id);
            free_slot(slot);
            if (now_empty) update_best_after_removal(o.is_bid, idx);
        }
    }

    std::optional<Price> best_bid() const {
        if (best_bid_idx_ == kNullSlot) return std::nullopt;
        return min_price_ + static_cast<Price>(best_bid_idx_);
    }

    std::optional<Price> best_ask() const {
        if (best_ask_idx_ == kNullSlot) return std::nullopt;
        return min_price_ + static_cast<Price>(best_ask_idx_);
    }

    Qty quantity_at(bool is_bid, Price price) const {
        size_t idx = tick_index(price);
        return is_bid ? bids_[idx].total_qty : asks_[idx].total_qty;
    }

    // Debug/demo helper: print up to depth price levels on each side.
    void print_depth(int depth) const {
        std::cout << std::fixed;
        std::cout << "---- ASK side (best first) ----\n";
        int shown = 0;
        if (best_ask_idx_ != kNullSlot) {
            for (int64_t j = best_ask_idx_;
                 j < static_cast<int64_t>(asks_.size()) && shown < depth; ++j) {
                if (!asks_[static_cast<size_t>(j)].empty()) {
                    std::cout << "  " << (min_price_ + j) << "  qty="
                              << asks_[static_cast<size_t>(j)].total_qty
                              << "  orders=" << asks_[static_cast<size_t>(j)].order_count
                              << '\n';
                    ++shown;
                }
            }
        }
        std::cout << "---- BID side (best first) ----\n";
        shown = 0;
        if (best_bid_idx_ != kNullSlot) {
            for (int64_t j = best_bid_idx_; j >= 0 && shown < depth; --j) {
                if (!bids_[static_cast<size_t>(j)].empty()) {
                    std::cout << "  " << (min_price_ + j) << "  qty="
                              << bids_[static_cast<size_t>(j)].total_qty
                              << "  orders=" << bids_[static_cast<size_t>(j)].order_count
                              << '\n';
                    ++shown;
                }
            }
        }
    }

private:
    size_t tick_index(Price price) const {
        return static_cast<size_t>(price - min_price_);
    }

    // ---- pooled order allocation (no heap alloc per order) ----

    int32_t alloc_slot() {
        if (!free_list_.empty()) {
            int32_t s = free_list_.back();
            free_list_.pop_back();
            return s;
        }
        order_pool_.emplace_back();
        return static_cast<int32_t>(order_pool_.size() - 1);
    }

    void free_slot(int32_t slot) { free_list_.push_back(slot); }

    // ---- order id -> pool slot ----
    // Dense array keyed directly by id. Swap for
    // std::unordered_map<OrderId, int32_t> if ids are sparse/huge.

    void register_id(OrderId id, int32_t slot) {
        if (id >= id_to_slot_.size()) id_to_slot_.resize(id + 1, kNullSlot);
        id_to_slot_[id] = slot;
    }

    void unregister_id(OrderId id) { id_to_slot_[id] = kNullSlot; }

    int32_t lookup_slot(OrderId id) const {
        if (id >= id_to_slot_.size()) return kNullSlot;
        return id_to_slot_[id];
    }

    // ---- intrusive doubly linked list, per level ----
    // Note: total_qty is deliberately NOT touched here. Keeping it out
    // of link/unlink means one function does one thing (pointer
    // bookkeeping), and callers who need partial-quantity semantics
    // (execute_order) aren't fighting an implicit side effect.

    void link_to_level(Level& level, int32_t slot) {
        Order& o = order_pool_[static_cast<size_t>(slot)];
        o.prev = level.tail;
        o.next = kNullSlot;
        if (level.tail != kNullSlot) {
            order_pool_[static_cast<size_t>(level.tail)].next = slot;
        } else {
            level.head = slot;
        }
        level.tail = slot;
        ++level.order_count;
    }

    void unlink_from_level(Level& level, int32_t slot) {
        Order& o = order_pool_[static_cast<size_t>(slot)];
        if (o.prev != kNullSlot) {
            order_pool_[static_cast<size_t>(o.prev)].next = o.next;
        } else {
            level.head = o.next;
        }
        if (o.next != kNullSlot) {
            order_pool_[static_cast<size_t>(o.next)].prev = o.prev;
        } else {
            level.tail = o.prev;
        }
        --level.order_count;
    }

    // ---- best bid/ask maintenance ----
    // Bids: higher index == higher price == better. Best bid is the
    // largest index with a non-empty level.
    // Asks: lower index == lower price == better. Best ask is the
    // smallest index with a non-empty level.

    void update_best_after_insert(bool is_bid, size_t idx) {
        if (is_bid) {
            if (best_bid_idx_ == kNullSlot ||
                static_cast<int64_t>(idx) > best_bid_idx_) {
                best_bid_idx_ = static_cast<int64_t>(idx);
            }
        } else {
            if (best_ask_idx_ == kNullSlot ||
                static_cast<int64_t>(idx) < best_ask_idx_) {
                best_ask_idx_ = static_cast<int64_t>(idx);
            }
        }
    }

    // Only walks the array when the level that just emptied WAS the
    // best level -- this is the O(distance-to-next-active-level) cost
    // flagged in the header comment. In practice this distance is small
    // because activity clusters near the touch, same empirical bet the
    // sorted-vector design made -- but here we only pay it when the
    // touch itself moves, not on every single add.
    void update_best_after_removal(bool is_bid, size_t idx) {
        if (is_bid) {
            if (best_bid_idx_ != static_cast<int64_t>(idx)) return;
            int64_t j = best_bid_idx_ - 1;
            while (j >= 0 && bids_[static_cast<size_t>(j)].empty()) --j;
            best_bid_idx_ = j;  // kNullSlot (-1) if book side is now empty
        } else {
            if (best_ask_idx_ != static_cast<int64_t>(idx)) return;
            int64_t j = best_ask_idx_ + 1;
            int64_t n = static_cast<int64_t>(asks_.size());
            while (j < n && asks_[static_cast<size_t>(j)].empty()) ++j;
            best_ask_idx_ = (j < n) ? j : kNullSlot;
        }
    }

    Price min_price_;
    Price max_price_;

    std::vector<Level> bids_;  // indexed directly by (price - min_price_)
    std::vector<Level> asks_;

    std::vector<Order>   order_pool_;
    std::vector<int32_t> free_list_;
    std::vector<int32_t> id_to_slot_;

    int64_t best_bid_idx_ = kNullSlot;
    int64_t best_ask_idx_ = kNullSlot;
};

// ---------------------------------------------------------------------
// Small demo driving the book through a sequence of ITCH-style events,
// to make the mechanics concrete.
// ---------------------------------------------------------------------
int main() {
    // Ticks 0..200 relative to min_price -> supports prices [10000, 10200]
    // e.g. a $100.00-$102.00 band quoted in whole cents.
    TickIndexedOrderBook book(10000, 10200);

    std::cout << "== add resting orders ==\n";
    book.add_order(1, /*is_bid=*/true,  10050, 100);  // bid 100.50 x100
    book.add_order(2, /*is_bid=*/true,  10050, 50);   // joins same level, FIFO after #1
    book.add_order(3, /*is_bid=*/true,  10045, 200);  // bid 100.45 x200
    book.add_order(4, /*is_bid=*/false, 10055, 75);   // ask 100.55 x75
    book.add_order(5, /*is_bid=*/false, 10060, 300);  // ask 100.60 x300
    book.print_depth(5);

    std::cout << "\nBest bid: " << *book.best_bid()
              << "  Best ask: " << *book.best_ask() << '\n';

    std::cout << "\n== partial execute order #1 (50 of 100) ==\n";
    book.execute_order(1, 50);
    std::cout << "qty at 10050 (bid side): " << book.quantity_at(true, 10050)
              << "  (order #1 keeps its queue spot, order #2 still behind it)\n";

    std::cout << "\n== cancel remainder of order #1 ==\n";
    book.cancel_order(1);
    std::cout << "qty at 10050 (bid side): " << book.quantity_at(true, 10050)
              << "  (only order #2's 50 shares remain)\n";

    std::cout << "\n== fully execute the best ask (order #4) ==\n";
    book.execute_order(4, 75);
    std::cout << "Best ask after #4 fully fills and the level empties: "
              << *book.best_ask()
              << "  (walked forward from 10055 to next active ask level)\n";

    std::cout << "\n== final depth ==\n";
    book.print_depth(5);

    return 0;
}
