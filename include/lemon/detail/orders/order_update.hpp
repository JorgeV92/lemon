#pragma once

#include <cstdint>
#include <optional>

#include <lemon/detail/orders/order_type.hpp>

namespace lemon {

struct OrderUpdate {
  OrderId order_id{};
  std::optional<Price> price{};
  std::optional<Quantity> quantity{};
  std::optional<Quantity> visible_quantity{};
  std::optional<Quantity> hidden_quantity{};
};

} // namespace lemon
