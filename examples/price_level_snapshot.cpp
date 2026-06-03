#include <iostream>
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

  std::cout << "price=" << level.price() << '\n';
  std::cout << "visible_quantity=" << level.visible_quantity() << '\n';
  std::cout << "hidden_quantity=" << level.hidden_quantity() << '\n';
  std::cout << "total_quantity=" << level.total_quantity() << '\n';
  std::cout << "checksum=" << package.get_checksum() << '\n';

  return 0;
}
