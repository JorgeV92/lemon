#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <lemon/detail/errors.hpp>
#include <lemon/detail/execution/trade.hpp>
#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

enum class TakerKind : std::uint8_t {
  Standard,
  PostOnly,
  MarketToLimit
};

enum class MatchOutcome : std::uint8_t {
  Filled,
  PartiallyFilled,
  NotFilled,
  Killed,
  Rejected
};

class MatchResult {
public:
  MatchResult() = default;

  MatchResult(OrderId order_id, Quantity initial_quantity)
    : order_id_(order_id),
      remaining_quantity_(initial_quantity),
      is_complete_(initial_quantity == Quantity::zero()),
      outcome_(is_complete_ ? MatchOutcome::Filled : MatchOutcome::NotFilled) {
  }

  static MatchResult with_capacity(
    OrderId order_id,
    Quantity initial_quantity,
    std::size_t capacity
  ) {
    MatchResult result{order_id, initial_quantity};
    result.trades_ = TradeList::with_capacity(capacity);
    result.filled_order_ids_.reserve(capacity);
    return result;
  }

  void add_trade(Trade trade) {
    if (trade.quantity().value() > remaining_quantity_.value()) {
      throw PriceLevelError("trade quantity exceeds remaining taker quantity");
    }

    remaining_quantity_ = Quantity{
      remaining_quantity_.value() - trade.quantity().value()
    };
    is_complete_ = remaining_quantity_ == Quantity::zero();
    outcome_ = is_complete_
      ? MatchOutcome::Filled
      : MatchOutcome::PartiallyFilled;
    trades_.add(std::move(trade));
  }

  void add_filled_order_id(OrderId order_id) {
    filled_order_ids_.push_back(order_id);
  }

  void finalize(Quantity remaining_quantity) {
    remaining_quantity_ = remaining_quantity;
    is_complete_ = remaining_quantity_ == Quantity::zero();
    if (is_complete_) {
      outcome_ = MatchOutcome::Filled;
    } else if (trades_.empty()) {
      outcome_ = MatchOutcome::NotFilled;
    } else {
      outcome_ = MatchOutcome::PartiallyFilled;
    }
  }

  void mark_killed(Quantity incoming_quantity) {
    trades_ = TradeList{};
    filled_order_ids_.clear();
    remaining_quantity_ = incoming_quantity;
    is_complete_ = false;
    outcome_ = MatchOutcome::Killed;
  }

  void mark_rejected(Quantity incoming_quantity) {
    trades_ = TradeList{};
    filled_order_ids_.clear();
    remaining_quantity_ = incoming_quantity;
    is_complete_ = false;
    outcome_ = MatchOutcome::Rejected;
  }

  OrderId order_id() const {
    return order_id_;
  }

  const TradeList& trades() const {
    return trades_;
  }

  Quantity remaining_quantity() const {
    return remaining_quantity_;
  }

  bool is_complete() const {
    return is_complete_;
  }

  const std::vector<OrderId>& filled_order_ids() const {
    return filled_order_ids_;
  }

  MatchOutcome outcome() const {
    return outcome_;
  }

  bool was_killed() const {
    return outcome_ == MatchOutcome::Killed;
  }

  bool was_rejected() const {
    return outcome_ == MatchOutcome::Rejected;
  }

  Quantity executed_quantity() const {
    value_type total = 0;
    for (const auto& trade : trades_.as_vector()) {
      if (trade.quantity().value() > std::numeric_limits<value_type>::max() - total) {
        throw PriceLevelError("executed quantity overflow");
      }
      total += trade.quantity().value();
    }
    return Quantity{total};
  }

  value_type executed_value() const {
    value_type total = 0;
    for (const auto& trade : trades_.as_vector()) {
      const value_type trade_value = trade.total_value();
      if (trade_value > std::numeric_limits<value_type>::max() - total) {
        throw PriceLevelError("executed value overflow");
      }
      total += trade_value;
    }
    return total;
  }

  std::optional<double> average_price() const {
    const value_type quantity = executed_quantity().value();
    if (quantity == 0) {
      return std::nullopt;
    }
    return static_cast<double>(executed_value()) /
           static_cast<double>(quantity);
  }

private:
  OrderId order_id_{};
  TradeList trades_{};
  Quantity remaining_quantity_{};
  bool is_complete_{false};
  std::vector<OrderId> filled_order_ids_{};
  MatchOutcome outcome_{MatchOutcome::NotFilled};
};

} // namespace lemon
