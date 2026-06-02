#pragma once

#include <cstdint>
#include <vector>

#include <lemon/detail/execution/trade.hpp>
#include <lemon/detail/orders/order.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

struct MatchResult {
  OrderId order_id{};
  TradeList trades{};
  Quantity remaining_quantity{};
  bool is_complete{false};
  std::vector<OrderId> filled_order_ids{};
};

} // namespace lemon
