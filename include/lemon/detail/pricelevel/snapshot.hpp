#pragma once

#include <string>
#include <vector>

#include <cstddef>

#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

struct PriceLevelSnapshot {
  Price price{};
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  std::size_t order_count{};
  std::vector<OrderType> orders{};
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
