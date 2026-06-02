#pragma once

#include <string>
#include <vector>

#include <lemon/detail/orders/order.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

struct PriceLevelSnapshot {
  Price price{};
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  std::size_t order_count{};
  std::vector<Order> orders{};
};

struct PriceLevelSnapshotPackage {
  PriceLevelSnapshot snapshot{};
  std::string checksum{};
};

struct PriceLevelData {
  Price price{};
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  std::size_t order_count{};
};

} // namespace lemon
