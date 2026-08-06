#pragma once
#ifndef SANDBOX_TEST_ENVIRONMENT_HPP
#define SANDBOX_TEST_ENVIRONMENT_HPP

#include "ankerl/unordered_dense.h"
#include "nanobench.h"
#include "fmt/format.h"
#include "bursa/meta.hpp"
#include "dataset.hpp"

namespace test
{
template <typename OrderBookType>
class TestEnvironment : public bursa::non_copyable
{
public:

    struct EnvRunInfo
    {
        size_t preFillAmount = 10'000;
        size_t numOperations = 10'000;
    };

    struct CommandBufferInfo
    {
        test::Message msg;
        std::string op;
        bursa::OrderInfo const* info;
    };

    TestEnvironment(OrderBookType& book, std::vector<bursa::OrderInfo> const& dataset) :
        orderBook{ book },
        ordersDataset{ dataset }
    {}

    ~TestEnvironment() = default;

    auto run(EnvRunInfo const& info = {}) -> void
    {
        // We want to pre-fill the order book with some amount of orders.
        // "seed" is hardcoded with the default 0xDEADBEEFu for now but ideally be set via the command line in the future.
        // The number of orders we're pre-filling the order book is hardcoded to a default of 10'000 for now.
        // Ideally that should be set through the command line as well.

        size_t const DATASET_SIZE = ordersDataset.size();

        // knownOrders prevents reusing a dataset entry. activeOrderIds contains
        // only orders that are still live in the simulated order book.
        ankerl::unordered_dense::map<bursa::order_id, bursa::OrderInfo const*> knownOrders(DATASET_SIZE);
        ankerl::unordered_dense::map<bursa::order_id, size_t> activeOrderIndices(DATASET_SIZE);
        std::vector<bursa::order_id> activeOrderIds;

        fmt::println("Filling the order book with {} order(s)", info.preFillAmount);

        activeOrderIds.reserve(info.numOperations);

        u32 seed = 0xDEADBEEFu;
        u32 inserted = {};

        while (inserted != info.preFillAmount)
        {
            auto const index = next_index(seed, DATASET_SIZE);

            auto& order = ordersDataset[index];
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
                // ++priceCount[order.price];
                ++inserted;
            }
        }

        fmt::println("Generating {} command(s)", info.numOperations);

        std::vector<CommandBufferInfo> commands;

        commands.reserve(info.numOperations);

        // TODO(afiq)
        for (size_t _ = {}; _ < info.numOperations; ++_)
        {
            auto msg = next_message(seed);

            // A cancellation or full execution requires a live order. If the
            // active pool is empty, turn this command into an add instead.
            if (activeOrderIds.empty() &&
                (msg == test::Message::Cancel ||
                 msg == test::Message::Execute))
            {
                msg = test::Message::Add_Bid;
            }

            if (msg == test::Message::Cancel ||
                msg == test::Message::Execute)
            {
                auto const index = next_index(seed, activeOrderIds.size());
                auto const id = activeOrderIds[index];

                commands.emplace_back(msg, (msg == test::Message::Cancel) ? "Cancel" : "Execute", knownOrders.at(id));

                // Execute uses the complete order quantity, so it removes the
                // order just like Cancel does.
                remove_active_order(activeOrderIds, activeOrderIndices, id);
            }
            else
            {
                size_t index = {};
                bursa::OrderInfo const* order = {};

                do
                {
                    index = next_index(seed, DATASET_SIZE);
                    order = &ordersDataset[index];
                }
                while (knownOrders.contains(order->id));

                commands.emplace_back(msg, (msg == test::Message::Add_Bid) ? "Add Bid" : "Add Ask", order);

                knownOrders.emplace(order->id, order);
                activeOrderIndices.emplace(order->id, activeOrderIds.size());
                activeOrderIds.push_back(order->id);
            }
        }

        fmt::println("Benchmarking ...");

        std::array sumMedianNanoseconds = { 0.0, 0.0, 0.0, 0.0 };
        std::array ops = { 0, 0, 0, 0 };

        ankerl::nanobench::Bench bench;
        bench.output(nullptr);

        for (auto&& [msg, op, info] : commands)
        {
            bench.run(
                op.c_str(),
                [msg, info, this]()
                {
                    TestEnvironment::callbacks[std::to_underlying(msg)](orderBook, *info);
                }
            );
            auto const& lastResult = bench.results().back();
            double const medianSeconds = lastResult.median(ankerl::nanobench::Result::Measure::elapsed);
            double const medianNanoseconds = medianSeconds * 1e9;
            sumMedianNanoseconds[std::to_underlying(msg)] += medianNanoseconds;
            ops[std::to_underlying(msg)]++;
        }

        fmt::println("Benchmark complete ...");

        print_stats(test::Message::Add_Ask, "Add Ask", sumMedianNanoseconds, ops);
        print_stats(test::Message::Add_Bid, "Add Bid", sumMedianNanoseconds, ops);
        print_stats(test::Message::Cancel, "Cancel", sumMedianNanoseconds, ops);
        print_stats(test::Message::Execute, "Execute", sumMedianNanoseconds, ops);
    }

private:

    OrderBookType& orderBook;
    std::span<bursa::OrderInfo const> ordersDataset;

    // "Branch-less" function invocation table.
    static constexpr std::array<void(*)(OrderBookType&, bursa::OrderInfo const&), 4> callbacks = {
        [](OrderBookType& book, bursa::OrderInfo const& info) -> void
        {
            book.add_bid_order(info);
        },
        [](OrderBookType& book, bursa::OrderInfo const& info) -> void
        {
            book.add_ask_order(info);
        },
        [](OrderBookType& book, bursa::OrderInfo const& info) -> void
        {
            book.cancel_order(info.id);
        },
        [](OrderBookType& book, bursa::OrderInfo const& info) -> void
        {
            book.execute_order(info.id, info.qty);
        },
    };

    auto next_message(u32& seed) -> test::Message
    {
        constexpr u64 RANDOM_DOMAIN     = 1ull << 32;
        constexpr u64 CANCEL_THRESHOLD  = RANDOM_DOMAIN * 60u / 100u;
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
    }

    auto next_index(u32& seed, size_t modulo) -> size_t
    {
        auto const rng = static_cast<size_t>(test::details::next_random(seed));
        return rng % modulo;
    }

    auto remove_active_order(std::vector<bursa::order_id>& activeOrderIds, ankerl::unordered_dense::map<bursa::order_id, size_t>& activeOrderIndices, bursa::order_id id) -> void
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

    auto print_stats(test::Message m, char const* label, std::span<double> sumMedianNanoseconds, std::span<int> ops) -> void
    {
        auto idx = std::to_underlying(m);
        double total = sumMedianNanoseconds[idx];
        int count = ops[idx];
        double avg = count ? (total / count) : 0.0;
        // align pipes by fixed-width fields, show at most 3 decimal places
        fmt::println("{:<12} | Total median = {:>12.3f}ns | Total ops = {:>6} | Average = {:>12.3f}ns", label, total, count, avg);
    }
};
// CTAD deduction guide.
template <typename OrderBookType>
TestEnvironment(OrderBookType&, std::vector<bursa::OrderInfo> const&) -> TestEnvironment<OrderBookType>;
}

#endif //!SANDBOX_TEST_ENVIRONMENT_HPP