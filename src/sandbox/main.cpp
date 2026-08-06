#include <string_view>

#include "mimalloc-new-delete.h"

#include "bursa/order_book.hpp"
#include "test_environment.hpp"

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

auto main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) -> int
{
    mi_version();

    // Ideally this should be set via the command line but an initial harcoded
    // value will serve our purpose for now.
    auto const DATASET_SIZE = size_t{ 100'000 };

    fmt::println("Generating {} dataset(s)", DATASET_SIZE);

    auto const dataset = test::make_dataset({
        .minPrice = 100'000u,
        .maxPrice = 150'000u,
        .minQuantity = 5u,
        .maxQuantity = 40u,
        .datasetSize = DATASET_SIZE
    });

    fmt::println("Initializing order book.");

    // Initialize the order book.
    auto orderBook = bursa::OrderBook<BenchmarkEnvironment>::from({
        .min = 100'000u,
        .max = 150'000u
    });

    test::TestEnvironment env0{ orderBook, dataset };

    env0.run();

    return 0;
}