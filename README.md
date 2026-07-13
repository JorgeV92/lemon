# Lemon

Lemon is an early-stage C++20 library for building a limit order book. Its
implemented core currently covers order variants, FIFO queue priority,
per-order matching behavior, price-level snapshots, and checksum-protected
snapshot packages.

Full `PriceLevel` mutation/matching and the public `OrderBook` workflow are
still being built. The sections below distinguish the working API from the
planned integration so examples do not imply that unfinished methods are
already available.

## Architecture

The major components have separate responsibilities:

| Component | Responsibility |
| --- | --- |
| `OrderType` | Stores a specific order variant and implements that maker's quantity behavior. |
| `OrderQueue` | Maintains FIFO insertion priority within one price level. |
| `PriceLevel` | Owns all resting orders at one price and tracks aggregate quantities. |
| `OrderBook` | Routes bid/ask orders across price levels and chooses which prices to match. |

The intended matching path is:

```text
incoming order
      |
      v
OrderBook: validate side/price/TIF and select crossing prices
      |
      v
PriceLevel: sweep one price in FIFO order
      |
      v
OrderQueue::match_front(): lock and select oldest maker
      |
      v
OrderType::match_against(): calculate fill/remainder/replenishment
      |
      v
PriceLevel: create trades and update quantities/statistics
```

`OrderType::match_against()` operates on quantity only. It assumes the caller
has already established that the incoming order crosses the maker's price and
is on the opposite side. That price/side routing belongs to `OrderBook` and
`PriceLevel`.

## Build

Lemon uses CMake and depends on Boost.JSON and OpenSSL.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Examples

Three executable examples are built by default:

```sh
./build/lemon_order_variants_example
./build/lemon_order_queue_matching_example
./build/lemon_price_level_snapshot_example
```

The complete sources are:

- [`examples/order_variants.cpp`](examples/order_variants.cpp)
- [`examples/order_queue_matching.cpp`](examples/order_queue_matching.cpp)
- [`examples/price_level_snapshot.cpp`](examples/price_level_snapshot.cpp)

### Order variants and matching

`OrderType` is backed by `std::variant` and supports:

- standard
- iceberg
- post-only
- trailing-stop
- pegged
- market-to-limit
- reserve

Construct variants through the named factories:

```cpp
constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;

auto standard = lemon::OrderType::standard(
  1,
  lemon::Side::Sell,
  lemon::Price{100},
  lemon::Quantity{10},
  timestamp
);

auto iceberg = lemon::OrderType::iceberg(
  2,
  lemon::Side::Sell,
  lemon::Price{100},
  lemon::Quantity{4},  // visible
  lemon::Quantity{12}, // hidden
  timestamp + 1
);

auto reserve = lemon::OrderType::reserve(
  3,
  lemon::Side::Sell,
  lemon::Price{100},
  lemon::Quantity{3},  // visible
  lemon::Quantity{20}, // hidden
  lemon::Quantity{2},  // replenish when visible falls below this threshold
  lemon::NonZeroQuantity{4},
  true,                // auto replenish
  timestamp + 2
);
```

`match_against()` returns four pieces of information:

```cpp
const lemon::OrderMatchResult result = iceberg.match_against(4);

// The incoming taker consumed all four visible units.
assert(result.consumed == 4);
assert(result.remaining_quantity == 0);

// Four hidden units became a new visible tranche.
assert(result.hidden_reduced == 4);
assert(result.updated_order.has_value());
assert(result.updated_order->get_visible_quantity() == lemon::Quantity{4});
assert(result.updated_order->get_hidden_quantity() == lemon::Quantity{8});
```

For a fully consumed standard order, `updated_order` is empty. For an ordinary
partial fill it contains the remaining maker. When an iceberg or reserve order
replenishes, `hidden_reduced` is positive; a price level uses that signal to
move the refreshed tranche to the back of its queue.

Reserve replenishment amounts cannot be zero:

```cpp
lemon::NonZeroQuantity valid{5};
// lemon::NonZeroQuantity invalid{0}; // throws std::invalid_argument
```

When a reserve does not specify an amount, Lemon uses the default tranche size
of 80, capped by the remaining hidden quantity.

### FIFO priority within a price level

`OrderQueue` stores one level's makers by monotonic insertion sequence. An
ordinary partial fill retains its sequence, while a replenished iceberg or
reserve receives a new tail sequence.

The queue exposes this distinction explicitly:

```cpp
std::unordered_set<lemon::InsertionSequence> set_aside;

auto result = queue.match_front(
  set_aside,
  [incoming_quantity](
    lemon::InsertionSequence,
    const lemon::OrderType& maker
  ) {
    auto match = maker.match_against(incoming_quantity);

    lemon::FrontAction action = lemon::FrontAction::remove();
    if (match.updated_order) {
      auto replacement =
        std::make_shared<lemon::OrderType>(*match.updated_order);

      action = match.hidden_reduced > 0
        ? lemon::FrontAction::replace_at_tail(std::move(replacement))
        : lemon::FrontAction::keep_in_place(std::move(replacement));
    }

    return std::pair{std::move(action), std::move(match)};
  }
);
```

The callback runs while the queue mutex is held. It must not call back into the
same queue or perform blocking work. `match_front()` commits the selected
action before unlocking, so cancellation cannot occur between deciding a
partial fill and storing its replacement.

Use these views depending on the required ordering:

- `snapshot_by_sequence()` returns the exact FIFO sweep order.
- `snapshot_vec()` sorts by `(timestamp, insertion sequence)` for deterministic
  persistence.

### PriceLevel snapshot example

The currently working `PriceLevel` workflow reconstructs a level from a
snapshot and exposes aggregate state:

```cpp
auto standard = std::make_shared<lemon::OrderType>(
  lemon::OrderType::standard(
    1, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{10}, timestamp
  )
);

auto iceberg = std::make_shared<lemon::OrderType>(
  lemon::OrderType::iceberg(
    2, lemon::Side::Buy, lemon::Price{100},
    lemon::Quantity{5}, lemon::Quantity{15}, timestamp + 1
  )
);

auto reserve = std::make_shared<lemon::OrderType>(
  lemon::OrderType::reserve(
    3, lemon::Side::Buy, lemon::Price{100},
    lemon::Quantity{4}, lemon::Quantity{12}, lemon::Quantity{2},
    lemon::NonZeroQuantity{3}, true, timestamp + 2
  )
);

auto snapshot = lemon::PriceLevelSnapshot::with_orders(
  lemon::Price{100},
  {standard, iceberg, reserve}
);
snapshot.refresh_aggregates();

const lemon::PriceLevel level = lemon::PriceLevel::from_snapshot(snapshot);
const lemon::PriceLevelSnapshotPackage package{snapshot};

assert(level.price() == lemon::Price{100});
assert(level.visible_quantity() == lemon::Quantity{19});
assert(level.hidden_quantity() == lemon::Quantity{27});
assert(level.total_quantity() == lemon::Quantity{46});
assert(level.order_count() == 3);

std::cout << package.to_json_string() << '\n';
```

The package JSON includes each order's variant and variant-specific state, such
as reserve replenishment configuration, pegged reference fields, and trailing
stop metadata. Its SHA-256 checksum is calculated over that snapshot payload.

## How PriceLevel will perform a complete match

Once the remaining `PriceLevel` methods are implemented, its matching loop will
compose the pieces that already work:

1. Receive the incoming quantity and taker metadata from `OrderBook`.
2. Ask `OrderQueue::match_front()` for the oldest maker.
3. Call `maker.match_against(remaining_quantity)`.
4. Create a trade for `consumed` quantity.
5. Remove a fully filled maker.
6. Keep an ordinary partial maker at its existing sequence.
7. Move a replenished iceberg/reserve maker to the tail.
8. Update visible/hidden aggregates, order count, and statistics.
9. Continue until the taker is filled or the level has no matchable depth.

The declarations for `PriceLevel::add_order()`, `match_order()`,
`update_order()`, and `snapshot()` exist, but their definitions are not yet
implemented. Do not call those methods yet; doing so will produce a linker
error. `PriceLevel::from_snapshot()` and the aggregate accessors are working.

## How OrderBook fits around PriceLevel

Conceptually, `OrderBook` owns two collections:

- bids, where the highest price has priority;
- asks, where the lowest price has priority.

For a new limit order, the complete book will:

1. Validate the order and its expiration policy.
2. Choose asks for an incoming buy or bids for an incoming sell.
3. Determine whether the best opposing price crosses the incoming limit.
4. Call `PriceLevel::match_order()` at each crossing price.
5. Remove empty price levels.
6. Rest any eligible remainder at its own price level.
7. Update the order-location index, last-trade price, and book statistics.

For a market order, it will repeat the same sweep without a limit-price check
until the requested quantity is filled or the opposing side is empty.

The `OrderBook` class is currently scaffolding. Its constructor,
market-close timestamp setters, and `best_bid()` are implemented; public order
insertion, ask queries, full matching, and book snapshots are still pending.
The examples therefore demonstrate its working building blocks rather than
claiming a complete end-to-end book API.

## Current status

Working:

- `Price` and `Quantity` value wrappers
- all seven `OrderType` variants
- per-order matchability, resizing, and matching
- iceberg and reserve replenishment
- FIFO `OrderQueue` mutation and priority rules
- deterministic queue snapshots
- `PriceLevelSnapshot` aggregate refresh
- variant-aware JSON snapshot packages and SHA-256 checksums
- `PriceLevel::from_snapshot()` and aggregate accessors

In progress:

- `PriceLevel::add_order()`
- `PriceLevel::iter_orders()` and `snapshot_orders()`
- `PriceLevel::match_order()`
- `PriceLevel::update_order()`
- `PriceLevel::snapshot()`
- full `OrderBook` insertion, queries, matching, and snapshots

See [`docs/status.md`](docs/status.md) for the concise implementation tracker.

## Repository layout

```text
.
├── examples
│   ├── order_queue_matching.cpp
│   ├── order_variants.cpp
│   └── price_level_snapshot.cpp
├── include/lemon
│   ├── detail/execution
│   ├── detail/orders
│   ├── detail/pricelevel
│   ├── detail/orderBook.hpp
│   └── utils
├── src/lemon
├── tests
└── docs/status.md
```
