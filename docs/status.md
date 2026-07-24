# Lemon Status

This document tracks what currently works and what is still scaffolded.

## Works

- `Price` and `Quantity` value wrappers.
- Variant-backed `OrderType` construction and read-only accessors for standard,
  iceberg, post-only, trailing-stop, pegged, market-to-limit, and reserve orders.
- Per-order matchability, partial/full matching, resizing, and
  iceberg/reserve replenishment.
- Time-in-force policy helper methods.
- `OrderQueue` construction, FIFO pop/removal, deterministic insertion-sequence
  ordering, priority-preserving and tail replacement, and locked front mutation.
- `PriceLevel` insertion, deterministic order views, matchable-depth queries,
  FIFO matching, aggregate tracking, and statistics.
- Standard partial/full fills, iceberg/reserve replenishment, post-only
  rejection, and IOC/FOK taker behavior.
- Quantity, price, combined, replacement, and cancellation updates with
  price-time priority rules.
- Trade generation and match results with outcomes, remaining quantity,
  filled maker IDs, executed value, and average price.
- Versioned `PriceLevelSnapshotPackage` JSON round trips with complete variant
  state, statistics, and SHA-256 validation.
- `PriceLevel` reconstruction from snapshots, packages, JSON, and data objects.
- `OrderBookEntry` accessors for price, visible quantity, total quantity, and order count.

## In Progress

- Most `OrderBook` query and matching functions.
- `OrderBookSnapshot` definition.
- Full unit test coverage for order-book behavior.

## Build Notes

The library is now built as a compiled static target because several methods are implemented in `src/lemon/*.cpp`.

Run the complete test suite with:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The complete `PriceLevel` example target is:

```sh
cmake --build build --target lemon_price_level_matching_example
./build/lemon_price_level_matching_example
```

## Sanitizer Checks

AddressSanitizer and UndefinedBehaviorSanitizer can be run together:

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan --parallel
ctest --test-dir build-asan --output-on-failure
```

ThreadSanitizer uses a separate build and is intentionally not part of the
portable default:

```sh
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build build-tsan --parallel
ctest --test-dir build-tsan --output-on-failure
```

On 2026-07-23, all 9 tests passed under both configurations on macOS arm64
with AppleClang 17.0.0. The ASan/UBSan run completed in 3.86 seconds and the
TSan run completed in 6.41 seconds with no reported sanitizer findings.
