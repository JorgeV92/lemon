# Lemon

Lemon is an early-stage C++20 library for building a limit order book. Its
implemented core covers order variants, FIFO queue priority, price-level
matching and updates, execution reporting and statistics, and
checksum-protected snapshots. The public `OrderBook` workflow is still being
built; `PriceLevel` is now usable independently or as its matching engine.

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

Four executable examples are built by default:

```sh
./build/lemon_order_variants_example
./build/lemon_order_queue_matching_example
./build/lemon_price_level_matching_example
./build/lemon_price_level_snapshot_example
```

The complete sources are:

- [`examples/order_variants.cpp`](examples/order_variants.cpp)
- [`examples/order_queue_matching.cpp`](examples/order_queue_matching.cpp)
- [`examples/price_level_matching.cpp`](examples/price_level_matching.cpp)
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

## PriceLevel end to end

A `PriceLevel` contains makers at exactly one price. `add_order()` rejects a
duplicate ID, assigns deterministic FIFO priority, and updates visible/hidden
aggregates and statistics. Orders should therefore have the same price as the
level; selecting a crossing price and verifying the opposing side remain the
responsibility of `OrderBook` or its caller.

This example adds standard and iceberg makers, fills them with an IOC taker,
updates a surviving order, and restores the final state from JSON:

```cpp
constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;
lemon::PriceLevel asks{lemon::Price{100}};

asks.add_order(lemon::OrderType::standard(
  1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{5}, timestamp
));
asks.add_order(lemon::OrderType::iceberg(
  2, lemon::Side::Sell, lemon::Price{100},
  lemon::Quantity{3}, lemon::Quantity{6}, timestamp + 1
));
asks.add_order(lemon::OrderType::standard(
  3, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{4}, timestamp + 2
));

lemon::TradeIdGenerator trade_ids{10'000};
auto result = asks.match_order(
  lemon::Quantity{8},
  900, // taker order ID
  lemon::TimeInForcePolicy{lemon::TimeInForce::ImmediateOrCancel},
  lemon::TakerKind::Standard,
  timestamp + 100,
  trade_ids
);

assert(result.outcome() == lemon::MatchOutcome::Filled);
assert(result.executed_quantity() == lemon::Quantity{8});
assert(result.trades().size() == 2);
assert(result.filled_order_ids() == std::vector<lemon::OrderId>{1});

// The iceberg replenished after its visible tranche was consumed, so it lost
// priority and moved behind maker 3.
auto fifo = asks.snapshot_by_insertion_sequence();
assert(fifo[0]->get_id() == 3);
assert(fifo[1]->get_id() == 2);

// A quantity increase also moves an order to the tail. A decrease retains its
// existing priority. A price change removes and returns the order so the book
// can insert it into the destination level.
asks.update_order(lemon::OrderUpdate::quantity(3, lemon::Quantity{7}));

const std::string json = asks.snapshot_to_json();
auto restored = lemon::PriceLevel::from_snapshot_json(json);
assert(restored.visible_quantity() == asks.visible_quantity());
assert(restored.hidden_quantity() == asks.hidden_quantity());
assert(restored.stats()->quantity_executed() == 8);
```

The complete runnable source is
[`examples/price_level_matching.cpp`](examples/price_level_matching.cpp).

### Matching behavior

`match_order()` sweeps makers in insertion-sequence order and returns a
`MatchResult` containing trades, the remaining taker quantity, completely
filled maker IDs, and one of `Filled`, `PartiallyFilled`, `NotFilled`, `Killed`,
or `Rejected`.

- An ordinary partial maker fill retains priority.
- A replenished iceberg/reserve maker moves to the tail.
- A post-only taker is rejected without consuming liquidity.
- Fill-or-kill performs a dry run, including replenishable hidden depth, before
  changing the level.
- Immediate-or-cancel executes available depth and reports any unfilled
  remainder; it never rests that remainder on the level.
- Non-matchable makers are skipped without disturbing their FIFO order.

The overload accepting an `OrderType` derives taker ID, quantity, TIF, kind,
and timestamp from the order:

```cpp
auto taker = lemon::OrderType::standard(
  900, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{6}, timestamp
);
auto result = asks.match_order(taker, trade_ids);
```

`PriceLevel` only performs the selected level's quantity match. It deliberately
does not decide whether the maker side and price cross; the surrounding book
must make that routing decision first. It also does not evict expired GTD/Day
makers; expiry enforcement belongs to that surrounding book layer.

### Updates and views

Use `OrderUpdate::quantity`, `price`, `price_and_quantity`, `cancel`, or
`replace` with `update_order()`. Quantity reductions retain time priority;
quantity increases move to the tail. Price-changing updates remove and return
the existing order; the surrounding book applies the requested price and
inserts it into the destination `PriceLevel`. Missing IDs return
`std::nullopt`.

`iter_orders()` and `snapshot_by_insertion_sequence()` expose FIFO order.
`snapshot_orders()` exposes a timestamp-oriented display view. Persistence uses
insertion/consumption order so demotions survive recovery.
`matchable_quantity(quantity, taker_id)` performs a non-mutating depth
calculation, excludes the taker's own resting ID, and is also used by
fill-or-kill matching.

### Snapshots and statistics

`snapshot()`, `snapshot_package()`, and `snapshot_to_json()` derive aggregates
from one materialized order vector, so the persisted order count and quantities
cannot disagree with the persisted orders. Statistics are captured alongside
that state as observability counters.
`from_snapshot()`, `from_snapshot_package()`, and `from_snapshot_json()` restore
that state. Package JSON is versioned and contains variant-specific state such
as reserve configuration, pegged references, and trailing-stop metadata. A
SHA-256 checksum is verified before packaged data is accepted.

Statistics include orders added/removed/executed, executed quantity and value,
first arrival, last execution, and cumulative maker waiting time. The
statistics object is available through `level.stats()`. Execution updates are
transactional; an unrecordable contribution sets the sticky
`stats_degraded()` indicator without invalidating an already committed trade.

## PriceLevel correctness and Rust comparison

Lemon's behavioral reference is
[`joaquinbejar/PriceLevel`](https://github.com/joaquinbejar/PriceLevel), pinned
at `5ee60e66378d5aa861a07e573bee19a6d48b4e97`. The hardening work adds terminal
self-match rejection, exact FIFO snapshot recovery, price/side topology
validation, checked admission/update/replenishment transitions, strongly safe
quantity updates, and transactional degradation-aware statistics.

Reproduce the optimized C++ and Rust run with:

```sh
scripts/run_pricelevel_benchmarks.sh
```

The recorded run used 500 operations, depth 256, one warmup, and five measured
trials on an 8-core Apple M1 iMac with 8 GB RAM, macOS 26.0.1, AppleClang
17.0.0, CMake 4.0.1, Rust 1.88.0, and Cargo 1.88.0. Values are median
operations/second:

| Workload | Lemon C++ | Rust PriceLevel | C++ / Rust |
| --- | ---: | ---: | ---: |
| Standard admission | 5,736,116 | 4,629,630 | 1.24× |
| FIFO standard matching | 4,360,564 | 1,915,809 | 2.28× |
| Partial fills | 7,100,659 | 2,969,562 | 2.39× |
| Iceberg replenishment | 3,105,590 | 743,034 | 4.18× |
| Cancellation | 4,325,858 | 1,218,398 | 3.55× |
| Quantity decrease | 4,194,349 | 3,379,315 | 1.24× |
| Quantity increase | 2,897,157 | 2,129,925 | 1.36× |
| Snapshot serialization | 517 | 805 | 0.64× |
| Snapshot restoration | 920 | 1,704 | 0.54× |
| Mixed single-thread | 11,115 | 20,542 | 0.54× |

In this run Lemon led the mutation-focused microbenchmarks, while Rust led
snapshot and mixed-workload throughput. Different languages, allocators, ID
representations, and synchronization architectures materially affect the
comparison; these machine-specific results are not universal performance
claims. Raw JSON, environment metadata, methodology, and the generated table
are in [`benchmarks/results`](benchmarks/results). See
[`docs/PRICELEVEL_PARITY.md`](docs/PRICELEVEL_PARITY.md) for remaining gaps.

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
- `PriceLevel` insertion, FIFO matching, updates, views, and aggregate tracking
- IOC, FOK, and post-only taker handling
- execution trades, results, and price-level statistics
- versioned, variant-aware JSON snapshot recovery and SHA-256 validation

In progress:

- full `OrderBook` insertion, queries, matching, and snapshots

See [`docs/status.md`](docs/status.md) for the concise implementation tracker.

## Repository layout

```text
.
├── examples
│   ├── order_queue_matching.cpp
│   ├── order_variants.cpp
│   ├── price_level_matching.cpp
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
