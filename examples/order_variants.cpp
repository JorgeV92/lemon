#include <iostream>
#include <vector>

#include <lemon/lemon.hpp>

namespace {

void print_order(const lemon::OrderType& order) {
  std::cout << "kind=" << lemon::to_string(order.kind())
            << " id=" << order.get_id()
            << " visible=" << order.get_visible_quantity()
            << " hidden=" << order.get_hidden_quantity()
            << " total=" << order.total_quantity()
            << " matchable=" << std::boolalpha << order.is_matchable()
            << '\n';
}

void print_match(const char* label, const lemon::OrderMatchResult& result) {
  std::cout << label
            << " consumed=" << result.consumed
            << " hidden_reduced=" << result.hidden_reduced
            << " incoming_remaining=" << result.remaining_quantity;

  if (result.updated_order) {
    std::cout << " maker_visible="
              << result.updated_order->get_visible_quantity()
              << " maker_hidden="
              << result.updated_order->get_hidden_quantity();
  } else {
    std::cout << " maker_filled=true";
  }
  std::cout << '\n';
}

} // namespace

int main() {
  constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;

  const std::vector<lemon::OrderType> orders{
    lemon::OrderType::standard(
      1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp
    ),
    lemon::OrderType::iceberg(
      2, lemon::Side::Sell, lemon::Price{100},
      lemon::Quantity{4}, lemon::Quantity{12}, timestamp
    ),
    lemon::OrderType::post_only(
      3, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{8}, timestamp
    ),
    lemon::OrderType::trailing_stop(
      4, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{7},
      lemon::Quantity{5}, lemon::Price{110}, timestamp
    ),
    lemon::OrderType::pegged(
      5, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{6},
      -1, lemon::PegReferenceType::BestAsk, timestamp
    ),
    lemon::OrderType::market_to_limit(
      6, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{5}, timestamp
    ),
    lemon::OrderType::reserve(
      7, lemon::Side::Sell, lemon::Price{100},
      lemon::Quantity{3}, lemon::Quantity{20}, lemon::Quantity{2},
      lemon::NonZeroQuantity{4}, true, timestamp
    )
  };

  std::cout << "orders\n";
  for (const auto& order : orders) {
    print_order(order);
  }

  print_match("standard_partial", orders[0].match_against(4));
  print_match("iceberg_visible_fill", orders[1].match_against(4));
  print_match("reserve_threshold", orders[6].match_against(2));

  return 0;
}
