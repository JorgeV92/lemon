#include <iostream>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include <lemon/lemon.hpp>

namespace {

void print_priority(const lemon::OrderQueue& queue, const char* label) {
  std::cout << label << ':';
  for (const auto& order : queue.snapshot_by_sequence()) {
    std::cout << " [id=" << order->get_id()
              << " visible=" << order->get_visible_quantity()
              << " hidden=" << order->get_hidden_quantity() << ']';
  }
  std::cout << '\n';
}

lemon::FrontAction action_for(const lemon::OrderMatchResult& match) {
  if (!match.updated_order) {
    return lemon::FrontAction::remove();
  }

  auto replacement = std::make_shared<lemon::OrderType>(*match.updated_order);
  if (match.hidden_reduced > 0) {
    // A replenished iceberg/reserve tranche receives a new tail sequence.
    return lemon::FrontAction::replace_at_tail(std::move(replacement));
  }

  // An ordinary partial fill keeps the maker's existing sequence.
  return lemon::FrontAction::keep_in_place(std::move(replacement));
}

lemon::OrderMatchResult match_one_front(
  lemon::OrderQueue& queue,
  std::uint64_t incoming_quantity
) {
  std::unordered_set<lemon::InsertionSequence> set_aside;
  auto result = queue.match_front(
    set_aside,
    [incoming_quantity](
      lemon::InsertionSequence,
      const lemon::OrderType& maker
    ) {
      auto match = maker.match_against(incoming_quantity);
      auto action = action_for(match);
      return std::pair{std::move(action), std::move(match)};
    }
  );

  if (!result) {
    throw std::runtime_error("cannot match an empty queue");
  }
  return std::move(*result);
}

} // namespace

int main() {
  constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;

  lemon::OrderQueue standard_queue;
  standard_queue.push(std::make_shared<lemon::OrderType>(
    lemon::OrderType::standard(
      1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp
    )
  ));
  standard_queue.push(std::make_shared<lemon::OrderType>(
    lemon::OrderType::standard(
      2, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp + 1
    )
  ));

  print_priority(standard_queue, "standard_before");
  const auto partial = match_one_front(standard_queue, 4);
  std::cout << "partial_consumed=" << partial.consumed << '\n';
  print_priority(standard_queue, "standard_after_partial");

  lemon::OrderQueue iceberg_queue;
  iceberg_queue.push(std::make_shared<lemon::OrderType>(
    lemon::OrderType::iceberg(
      3, lemon::Side::Sell, lemon::Price{100},
      lemon::Quantity{3}, lemon::Quantity{6}, timestamp
    )
  ));
  iceberg_queue.push(std::make_shared<lemon::OrderType>(
    lemon::OrderType::standard(
      4, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp + 1
    )
  ));

  print_priority(iceberg_queue, "iceberg_before");
  const auto replenish = match_one_front(iceberg_queue, 3);
  std::cout << "iceberg_consumed=" << replenish.consumed
            << " replenished=" << replenish.hidden_reduced << '\n';
  print_priority(iceberg_queue, "iceberg_after_replenish");

  return 0;
}
