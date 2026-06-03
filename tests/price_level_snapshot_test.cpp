#include <cassert>
#include <memory>
#include <string>
#include <vector>

#include <lemon/lemon.hpp>

int main() {
  auto order_a = std::make_shared<lemon::OrderType>(
    1,
    lemon::Side::Buy,
    lemon::Price{100},
    lemon::Quantity{10},
    lemon::Quantity{7},
    lemon::Quantity{3},
    1717286400000
  );

  auto order_b = std::make_shared<lemon::OrderType>(
    2,
    lemon::Side::Buy,
    lemon::Price{100},
    lemon::Quantity{4},
    lemon::Quantity{4},
    lemon::Quantity{0},
    1717286400001
  );

  std::vector<std::shared_ptr<lemon::OrderType>> orders{order_a, order_b};
  auto snapshot = lemon::PriceLevelSnapshot::with_orders(lemon::Price{100}, orders);
  snapshot.refresh_aggregates();

  assert(snapshot.price() == lemon::Price{100});
  assert(snapshot.visible_quantity() == lemon::Quantity{11});
  assert(snapshot.hidden_quantity() == lemon::Quantity{3});
  assert(snapshot.total_quantity() == lemon::Quantity{14});
  assert(snapshot.order_count() == 2);

  const lemon::PriceLevel level = lemon::PriceLevel::from_snapshot(snapshot);
  assert(level.price() == lemon::Price{100});
  assert(level.visible_quantity() == lemon::Quantity{11});
  assert(level.hidden_quantity() == lemon::Quantity{3});
  assert(level.total_quantity() == lemon::Quantity{14});
  assert(level.order_count() == 2);

  const lemon::PriceLevelSnapshotPackage package{snapshot};
  assert(package.get_checksum().size() == 64);

  const std::string payload = package.to_json_string();
  assert(payload.find("\"checksum\"") != std::string::npos);
  assert(payload.find("\"orders\"") != std::string::npos);

  return 0;
}
