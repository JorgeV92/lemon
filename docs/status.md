# Lemon Status

This document tracks what currently works and what is still scaffolded.

## Works

- `Price` and `Quantity` value wrappers.
- `OrderType` construction and read-only accessors.
- Time-in-force policy helper methods.
- `OrderQueue` construction from `std::vector<std::shared_ptr<OrderType>>`.
- `PriceLevelSnapshot` aggregate refresh for visible quantity, hidden quantity, and order count.
- `PriceLevelSnapshotPackage` JSON serialization and SHA-256 checksum generation.
- `PriceLevel::from_snapshot()` reconstruction for price, quantities, order count, and order queue state.
- `OrderBookEntry` accessors for price, visible quantity, total quantity, and order count.

## In Progress

- `PriceLevel::add_order`.
- `PriceLevel::iter_orders`.
- `PriceLevel::snapshot_orders`.
- `PriceLevel::match_order`.
- `PriceLevel::update_order`.
- `PriceLevel::snapshot`.
- Most `OrderBook` query and matching functions.
- `OrderBookSnapshot` definition.
- Full unit test coverage for matching, updates, snapshots, and order-book behavior.

## Build Notes

The library is now built as a compiled static target because several methods are implemented in `src/lemon/*.cpp`.

The current working test is:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

The current example target is:

```sh
cmake --build build --target lemon_price_level_snapshot_example
./build/lemon_price_level_snapshot_example
```
