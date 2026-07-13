# Lemon

<!-- ![Lemon](img/lemon.png) -->

Lemon is an early-stage C++ order book library focused on representing market state at a point in time. The current implementation is centered on orders, price levels, snapshots, and checksum-backed snapshot packages.

The project is intentionally small while the core model is being built. Some APIs are implemented and tested; matching and full order-book behavior are still in progress.

## Getting Started

Lemon uses CMake and currently depends on Boost.JSON and OpenSSL.

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the example:

```sh
./build/lemon_price_level_snapshot_example
```

Expected output shape:

```txt
price=100
visible_quantity=8
hidden_quantity=2
total_quantity=10
checksum=...
```

## Example

```cpp
#include <memory>
#include <vector>

#include <lemon/lemon.hpp>

int main() {
  auto order = std::make_shared<lemon::OrderType>(
    1,
    lemon::Side::Buy,
    lemon::Price{100},
    lemon::Quantity{10},
    lemon::Quantity{8},
    lemon::Quantity{2},
    1717286400000
  );

  auto snapshot = lemon::PriceLevelSnapshot::with_orders(
    lemon::Price{100},
    std::vector<std::shared_ptr<lemon::OrderType>>{order}
  );

  snapshot.refresh_aggregates();

  const lemon::PriceLevel level = lemon::PriceLevel::from_snapshot(snapshot);
  const lemon::PriceLevelSnapshotPackage package{snapshot};
}
```

## Status

Working:

- `Price` and `Quantity` value wrappers
- `OrderType` construction and accessors
- `OrderQueue` FIFO insertion/removal, deterministic sequence ordering,
  priority-preserving and tail replacement, and locked front mutation
- `PriceLevelSnapshot` aggregate refresh
- `PriceLevelSnapshotPackage` JSON serialization and SHA-256 checksums
- `PriceLevel::from_snapshot`

In progress:

- order insertion and updates
- matching
- full order-book queries
- `OrderBookSnapshot`
- broader tests

See [docs/status.md](docs/status.md) for the current implementation status.

## File Structure

```txt
.
├── CMakeLists.txt
├── README.md
├── docs
│   └── status.md
├── examples
│   └── price_level_snapshot.cpp
├── include
│   └── lemon
│       ├── lemon.hpp
│       ├── detail
│       │   ├── execution
│       │   ├── orders
│       │   ├── pricelevel
│       │   └── orderBook.hpp
│       └── utils
├── src
│   └── lemon
│       ├── entry.cpp
│       ├── level.cpp
│       └── orderBook.cpp
└── tests
    └── price_level_snapshot_test.cpp
```
