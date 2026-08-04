#include <array>
#include <cassert>
#include <cstddef>
#include <ranges>
#include <string_view>
#include <utility>
#include <vector>

#include "mimalloc-new-delete.h"
#include "fmt/format.h"
#include "ankerl/unordered_dense.h"

#include "nanobench.h"

#include "bursa/order_book.hpp"

#include "dataset.hpp"

// Pre-population vs. Steady State: Don't just inject 100,000 orders all at once unless you are only testing bulk insertion. For a realistic steady-state benchmark, pre-populate the book with an initial state (e.g., 10,000 resting orders) so there is a live depth to match against, then stream your 100,000 benchmark commands.
// If your data distribution is completely uniform (e.g., random prices scattered across a 10,000-tick range), your benchmark won't reflect reality. Real markets exhibit heavy clustering.
// Gaussian / Normal or Cauchy Distribution: Center your limit order prices tightly around a shifting "mid-price."
//  - Why it matters: If prices are uniformly scattered, orders will rarely cross, and you'll never adequately test your matching engine path. Clustering orders within $\pm 5$ to $\pm 20$ ticks of the current best bid/ask ensures constant matching, queue adjustments, and top-of-book contention.
//
// Real markets have vastly more cancellations and modifications than executions. A representative workload ratio looks like this:
//  - 60% – 70% Cancellations
//  - 20% – 25% Limit Orders (Adds)
//  - 5% – 10% Market Orders (Aggressive takes)
//
// ┌────────────────────────────────────────────────────────┐
// │                  1. Warmup Phase                       │
// │  Pre-fill book with N initial orders to establish depth │
// └──────────────────────────┬─────────────────────────────┘
                            // │
                            // ▼
// ┌────────────────────────────────────────────────────────┐
// │               2. Benchmark Generator                   │
// │  Roll weighted dice (e.g. 65% Cancel, 25% Add, 10% Match) │
// │                                                        │
// │  - If Add:    Generate clustered price, save ID to Pool│
// │  - If Cancel: Pop random ID from Active ID Pool        │
// │  - If Market: Generate fill against current spread     │
// └──────────────────────────┬─────────────────────────────┘
                            // │
                            // ▼
// ┌────────────────────────────────────────────────────────┐
// │                3. Run Benchmarks                       │
// │  Execute pre-allocated Command Vector in tight loop    │
// └────────────────────────────────────────────────────────┘
//

struct AAPL;

struct BenchmarkEnvironment
{
    using instrument_type = AAPL;
};

namespace bursa
{
template <>
struct instrument_id<AAPL>
{
    static constexpr std::string_view value = "AAPL";
};
}

struct CommandBufferInfo
{
    test::Message msg;
    bursa::OrderInfo const* info;
};

auto next_message(u32& seed) -> test::Message
{
    constexpr u64 RANDOM_DOMAIN = 1ull << 32;
    constexpr u64 CANCEL_THRESHOLD = RANDOM_DOMAIN * 60u / 100u;
    constexpr u64 ADD_BID_THRESHOLD = RANDOM_DOMAIN * 75u / 100u;
    constexpr u64 ADD_ASK_THRESHOLD = RANDOM_DOMAIN * 90u / 100u;

    auto const roll = test::details::next_random(seed);

    if (roll < CANCEL_THRESHOLD)
    {
        return test::Message::Cancel;
    }
    else if (roll < ADD_BID_THRESHOLD)
    {
        return test::Message::Add_Bid;
    }
    else if (roll < ADD_ASK_THRESHOLD)
    {
        return test::Message::Add_Ask;
    }
    else
    {
        return test::Message::Execute;
    }
};

auto next_index(u32& seed, size_t modulo) -> size_t
{
    assert(modulo > 0);

    auto const rng = static_cast<size_t>(test::details::next_random(seed));
    return rng % modulo;
}

auto remove_active_order(
    std::vector<bursa::order_id>& activeOrderIds,
    ankerl::unordered_dense::map<bursa::order_id, size_t>& activeOrderIndices,
    bursa::order_id id) -> void
{
    auto const it = activeOrderIndices.find(id);

    if (it == activeOrderIndices.end())
    {
        return;
    }

    auto const removedIndex = it->second;
    auto const lastIndex = activeOrderIds.size() - 1u;

    if (removedIndex != lastIndex)
    {
        auto const lastId = activeOrderIds[lastIndex];
        activeOrderIds[removedIndex] = lastId;
        activeOrderIndices[lastId] = removedIndex;
    }

    activeOrderIds.pop_back();
    activeOrderIndices.erase(id);
}

auto main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) -> int
{
    mi_version();

    // "Branch-less" function table invocation.
    constexpr std::array<void(*)(bursa::OrderBook<BenchmarkEnvironment>&, bursa::OrderInfo const&), 4> callbacks = {
        [](bursa::OrderBook<BenchmarkEnvironment>& book, bursa::OrderInfo const& info) -> void
        {
            book.add_bid_order(info);
        },
        [](bursa::OrderBook<BenchmarkEnvironment>& book, bursa::OrderInfo const& info) -> void
        {
            book.add_ask_order(info);
        },
        [](bursa::OrderBook<BenchmarkEnvironment>& book, bursa::OrderInfo const& info) -> void
        {
            book.cancel_order(info.id);
        },
        [](bursa::OrderBook<BenchmarkEnvironment>& book, bursa::OrderInfo const& info) -> void
        {
            book.execute_order(info.id, info.qty);
        },
    };

    // Ideally this should be set via the command line but an initial harcoded
    // value will serve our purpose for now.
    auto const DATASET_SIZE = size_t{ 100'000 };

    fmt::println("Generating {} dataset(s)", DATASET_SIZE);

    auto const dataset = test::make_dataset({
        .minPrice = 100'000u,
        .maxPrice = 100'100u,
        .minQuantity = 5u,
        .maxQuantity = 10u,
        .datasetSize = DATASET_SIZE
    });

    fmt::println("Initializing order book.");

    // Initialize the order book.
    auto orderBook = bursa::OrderBook<BenchmarkEnvironment>::from({
        .min = 100'000u,
        .max = 150'000u
    });

    // We want to pre-fill the order book with some amount of orders.
    // "seed" is hardcoded with the default 0xDEADBEEFu for now but ideally be set via the command line in the future.
    // The number of orders we're pre-filling the order book is hardcoded to a default of 10'000 for now.
    // Ideally that should be set through the command line as well.

    fmt::println("Filling the orde book with {} order(s)", 10'000);

    // knownOrders prevents reusing a dataset entry. activeOrderIds contains
    // only orders that are still live in the simulated order book.
    ankerl::unordered_dense::map<bursa::order_id, bursa::OrderInfo const*> knownOrders(DATASET_SIZE);
    ankerl::unordered_dense::map<bursa::order_id, size_t> activeOrderIndices(DATASET_SIZE);
    ankerl::unordered_dense::map<u32, u32> priceCount(DATASET_SIZE);    // Mostly for debugging.
    std::vector<bursa::order_id> activeOrderIds;

    activeOrderIds.reserve(10'000);

    u32 seed = 0xDEADBEEFu;
    u32 inserted = {};

    while (inserted != 10'000)
    {
        auto const index = next_index(seed, DATASET_SIZE);

        auto& order = dataset[index];
        if (!knownOrders.contains(order.id))
        {
            if (index % 2 == 0)
            {
                orderBook.add_bid_order(order);
            }
            else
            {
                orderBook.add_ask_order(order);
            }
            knownOrders.emplace(order.id, &order);
            activeOrderIndices.emplace(order.id, activeOrderIds.size());
            activeOrderIds.push_back(order.id);
            ++priceCount[order.price];
            ++inserted;
        }
    }

    // Rather than doing all the logic during benchmarking, we pre-fill a buffer of commands that we'll use during benchmarking.
    // This one, we're minimizing the scope of the benchmark to specifically the order book.

    fmt::println("Generating {} command(s)", 10'000);

    std::vector<CommandBufferInfo> commands;

    commands.reserve(10'000);

    for (auto _ : std::views::iota(0, 10'000))
    {
        auto nextMessage = next_message(seed);

        // A cancellation or full execution requires a live order. If the
        // active pool is empty, turn this command into an add instead.
        if (activeOrderIds.empty() &&
            (nextMessage == test::Message::Cancel ||
             nextMessage == test::Message::Execute))
        {
            nextMessage = test::Message::Add_Bid;
        }

        if (nextMessage == test::Message::Cancel ||
            nextMessage == test::Message::Execute)
        {
            auto const index = next_index(seed, activeOrderIds.size());
            auto const id = activeOrderIds[index];

            commands.emplace_back(nextMessage, knownOrders.at(id));

            // Execute uses the complete order quantity, so it removes the
            // order just like Cancel does.
            remove_active_order(activeOrderIds, activeOrderIndices, id);
        }
        else
        {
            assert(
                nextMessage == test::Message::Add_Bid ||
                nextMessage == test::Message::Add_Ask);

            size_t index = {};
            bursa::OrderInfo const* order = {};

            do
            {
                index = next_index(seed, DATASET_SIZE);
                order = &dataset[index];
            }
            while (knownOrders.contains(order->id));

            commands.emplace_back(nextMessage, order);

            knownOrders.emplace(order->id, order);
            activeOrderIndices.emplace(order->id, activeOrderIds.size());
            activeOrderIds.push_back(order->id);
            ++priceCount[order->price];
        }
    }

    fmt::println("Benchmarking ...");

    ankerl::nanobench::Bench().run(
        "Running 10'000 commands",
        [&commands, &orderBook, &callbacks]()
        {
            for (auto&& [msg, info] : commands)
            {
                callbacks[std::to_underlying(msg)](orderBook, *info);
            }
        }
    );

    fmt::println("Benchmark complete ...");

    return 0;
}