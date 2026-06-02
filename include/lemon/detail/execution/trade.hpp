#pragma once

#include <cstdint>
#include <vector>

#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

using TradeId = std::uint64_t;

struct Trade {
  TradeId trade_id{};
  OrderId taker_order_id{};
  OrderId maker_order_id{};
  Price price{};
  Quantity quantity{};
  Side taker_side{};
  TimestampMs timestamp{};
};

struct TradeList {
  std::vector<Trade> trades;
};

} // namespace lemon
