#include <iostream>
#include <memory>
#include <vector>

#include <lemon/lemon.hpp>

int main() {
  constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;

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
    std::vector<std::shared_ptr<lemon::OrderType>>{
      standard,
      iceberg,
      reserve
    }
  );
  snapshot.refresh_aggregates();

  const lemon::PriceLevel level = lemon::PriceLevel::from_snapshot(snapshot);
  const lemon::PriceLevelSnapshotPackage package{snapshot};

  std::cout << "price=" << level.price() << '\n';
  std::cout << "visible_quantity=" << level.visible_quantity() << '\n';
  std::cout << "hidden_quantity=" << level.hidden_quantity() << '\n';
  std::cout << "total_quantity=" << level.total_quantity() << '\n';
  std::cout << "order_count=" << level.order_count() << '\n';
  std::cout << "checksum=" << package.get_checksum() << '\n';
  std::cout << "package=" << package.to_json_string() << '\n';

  return 0;
}
