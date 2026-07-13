#include <iostream>

#include <lemon/lemon.hpp>

namespace {

const char* outcome_name(lemon::MatchOutcome outcome) {
  switch (outcome) {
    case lemon::MatchOutcome::Filled:
      return "filled";
    case lemon::MatchOutcome::PartiallyFilled:
      return "partially-filled";
    case lemon::MatchOutcome::NotFilled:
      return "not-filled";
    case lemon::MatchOutcome::Killed:
      return "killed";
    case lemon::MatchOutcome::Rejected:
      return "rejected";
  }
  return "unknown";
}

void print_level(const char* label, const lemon::PriceLevel& level) {
  std::cout << label
            << " price=" << level.price()
            << " visible=" << level.visible_quantity()
            << " hidden=" << level.hidden_quantity()
            << " orders=" << level.order_count()
            << " fifo=";

  for (const auto& order : level.snapshot_by_insertion_sequence()) {
    std::cout << ' ' << order->get_id()
              << '(' << lemon::to_string(order->kind())
              << ":" << order->get_visible_quantity()
              << '+' << order->get_hidden_quantity() << ')';
  }
  std::cout << '\n';
}

void print_result(const lemon::MatchResult& result) {
  std::cout << "outcome=" << outcome_name(result.outcome())
            << " executed=" << result.executed_quantity()
            << " remaining=" << result.remaining_quantity()
            << " trades=" << result.trades().size() << '\n';

  for (const auto& trade : result.trades().as_vector()) {
    std::cout << "  trade=" << trade.trade_id()
              << " maker=" << trade.maker_order_id()
              << " taker=" << trade.taker_order_id()
              << " quantity=" << trade.quantity()
              << " price=" << trade.price() << '\n';
  }
}

} // namespace

int main() {
  constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;
  lemon::PriceLevel asks{lemon::Price{100}};

  asks.add_order(lemon::OrderType::standard(
    1,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity{5},
    timestamp
  ));
  asks.add_order(lemon::OrderType::iceberg(
    2,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity{3},
    lemon::Quantity{6},
    timestamp + 1
  ));
  asks.add_order(lemon::OrderType::standard(
    3,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity{4},
    timestamp + 2
  ));

  print_level("before", asks);

  lemon::TradeIdGenerator trade_ids{10'000};
  const auto result = asks.match_order(
    lemon::Quantity{8},
    900,
    lemon::TimeInForcePolicy{lemon::TimeInForce::ImmediateOrCancel},
    lemon::TakerKind::Standard,
    timestamp + 100,
    trade_ids
  );

  print_result(result);
  print_level("after match", asks);

  // Maker 2 replenished its iceberg tranche and moved behind maker 3. A
  // quantity increase also loses time priority and moves to the queue tail.
  asks.update_order(lemon::OrderUpdate::quantity(3, lemon::Quantity{7}));
  print_level("after update", asks);

  const std::string json = asks.snapshot_to_json();
  const auto restored = lemon::PriceLevel::from_snapshot_json(json);
  print_level("restored", restored);

  std::cout << "executions=" << restored.stats()->orders_executed()
            << " executed_quantity=" << restored.stats()->quantity_executed()
            << " json_bytes=" << json.size() << '\n';

  return 0;
}
