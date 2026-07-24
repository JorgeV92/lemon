#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>

#include <lemon/lemon.hpp>

namespace {

lemon::OrderType maker(
  lemon::OrderId id,
  std::uint64_t quantity,
  lemon::TimestampMs timestamp
) {
  return lemon::OrderType::standard(
    id,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity{quantity},
    timestamp
  );
}

void verify_invariants(const lemon::PriceLevel& level) {
  const auto orders = level.snapshot_by_insertion_sequence();
  std::unordered_set<lemon::OrderId> ids;
  std::optional<lemon::Side> side;
  std::uint64_t visible = 0;
  std::uint64_t hidden = 0;

  for (const auto& order : orders) {
    assert(order);
    assert(order->get_price() == level.price());
    assert(ids.insert(order->get_id()).second);
    if (side) {
      assert(order->get_side() == *side);
    }
    side = order->get_side();
    assert(order->get_visible_quantity().value() <=
           std::numeric_limits<std::uint64_t>::max() - visible);
    assert(order->get_hidden_quantity().value() <=
           std::numeric_limits<std::uint64_t>::max() - hidden);
    visible += order->get_visible_quantity().value();
    hidden += order->get_hidden_quantity().value();
  }

  assert(level.order_count() == orders.size());
  assert(level.visible_quantity() == lemon::Quantity{visible});
  assert(level.hidden_quantity() == lemon::Quantity{hidden});
}

void assert_trade_order(
  const std::vector<std::shared_ptr<lemon::OrderType>>& before,
  lemon::Quantity requested,
  const lemon::MatchResult& result
) {
  std::vector<lemon::OrderId> expected;
  std::uint64_t remaining = requested.value();
  for (const auto& order : before) {
    if (remaining == 0) {
      break;
    }
    const std::uint64_t consumed = std::min(
      remaining,
      order->get_visible_quantity().value()
    );
    if (consumed > 0) {
      expected.push_back(order->get_id());
      remaining -= consumed;
    }
  }

  std::vector<lemon::OrderId> actual;
  for (const auto& trade : result.trades().as_vector()) {
    actual.push_back(trade.maker_order_id());
  }
  assert(actual == expected);
}

void run_randomized_sequence(std::uint64_t seed) {
  std::cerr << "price_level randomized seed=" << seed << '\n';
  std::mt19937_64 random{seed};
  auto level = std::make_unique<lemon::PriceLevel>(lemon::Price{100});
  lemon::OrderId next_id = 1;
  lemon::TradeIdGenerator trade_ids{1};

  for (std::uint64_t step = 0; step < 500; ++step) {
    const auto before = level->snapshot_by_insertion_sequence();
    const std::uint64_t operation = random() % 7;

    if (operation == 0 || before.empty()) {
      level->add_order(maker(next_id++, random() % 20 + 1, step + 1));
    } else if (operation == 1) {
      const auto& selected = before[random() % before.size()];
      static_cast<void>(level->update_order(
        lemon::OrderUpdate::cancel(selected->get_id())
      ));
    } else if (operation == 2) {
      const auto& selected = before[random() % before.size()];
      static_cast<void>(level->update_order(
        lemon::OrderUpdate::quantity(
          selected->get_id(),
          lemon::Quantity{random() % 25 + 1}
        )
      ));
    } else if (operation == 3) {
      const lemon::Quantity requested{random() % 10 + 1};
      const auto result = level->match_order(
        requested,
        1'000'000 + step,
        {},
        lemon::TakerKind::Standard,
        10'000 + step,
        trade_ids
      );
      assert_trade_order(before, requested, result);
    } else if (operation == 4) {
      const std::string payload = level->snapshot_to_json();
      level = std::unique_ptr<lemon::PriceLevel>{
        new lemon::PriceLevel(
          lemon::PriceLevel::from_snapshot_json(payload)
        )
      };
    } else if (operation == 5) {
      const std::string unchanged = level->snapshot_to_json();
      bool rejected = false;
      try {
        level->add_order(lemon::OrderType::standard(
          next_id,
          lemon::Side::Sell,
          lemon::Price{101},
          lemon::Quantity{1},
          step + 1
        ));
      } catch (const lemon::PriceLevelError&) {
        rejected = true;
      }
      assert(rejected);
      assert(level->snapshot_to_json() == unchanged);
    } else {
      const std::string unchanged = level->snapshot_to_json();
      bool rejected = false;
      try {
        level->add_order(maker(
          before.front()->get_id(),
          1,
          step + 1
        ));
      } catch (const lemon::PriceLevelError&) {
        rejected = true;
      }
      assert(rejected);
      assert(level->snapshot_to_json() == unchanged);
    }

    verify_invariants(*level);
  }
}

void cancellation_and_matching_are_serialized() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 100, 1));
  lemon::TradeIdGenerator trade_ids{1};
  std::atomic<bool> start{false};
  std::optional<lemon::MatchResult> result;

  std::thread matcher([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    result = level.match_order(
      lemon::Quantity{50}, 99, {}, lemon::TakerKind::Standard, 100, trade_ids
    );
  });
  std::thread canceler([&] {
    while (!start.load(std::memory_order_acquire)) {
    }
    static_cast<void>(level.update_order(lemon::OrderUpdate::cancel(1)));
  });
  start.store(true, std::memory_order_release);
  matcher.join();
  canceler.join();

  assert(result);
  assert(result->trades().size() <= 1);
  assert(level.order_count() == 0);
  verify_invariants(level);
}

void fok_and_post_only_decisions_are_serialized() {
  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(maker(1, 5, 1));
    level.add_order(maker(2, 5, 2));
    lemon::TradeIdGenerator trade_ids{1};
    std::atomic<bool> start{false};
    std::optional<lemon::MatchResult> result;
    std::thread matcher([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      result = level.match_order(
        lemon::Quantity{10},
        99,
        lemon::TimeInForcePolicy{lemon::TimeInForce::FillOrKill},
        lemon::TakerKind::Standard,
        100,
        trade_ids
      );
    });
    std::thread canceler([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      static_cast<void>(level.update_order(lemon::OrderUpdate::cancel(1)));
    });
    start.store(true, std::memory_order_release);
    matcher.join();
    canceler.join();
    assert(result);
    assert(result->was_killed() || result->is_complete());
    assert(result->trades().empty() || result->trades().size() == 2);
    verify_invariants(level);
  }

  {
    lemon::PriceLevel level{lemon::Price{100}};
    lemon::TradeIdGenerator trade_ids{1};
    std::atomic<bool> start{false};
    std::optional<lemon::MatchResult> result;
    std::thread matcher([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      result = level.match_order(
        lemon::Quantity{1},
        99,
        {},
        lemon::TakerKind::PostOnly,
        100,
        trade_ids
      );
    });
    std::thread adder([&] {
      while (!start.load(std::memory_order_acquire)) {
      }
      level.add_order(maker(1, 1, 1));
    });
    start.store(true, std::memory_order_release);
    matcher.join();
    adder.join();
    assert(result);
    assert(result->trades().empty());
    assert(level.order_count() == 1);
    verify_invariants(level);
  }
}

void statistics_snapshots_are_not_torn() {
  lemon::PriceLevelStatistics statistics;
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for (std::uint64_t index = 1; index <= 2'000; ++index) {
      statistics.record_execution(1, 2, 0, index);
    }
    done.store(true, std::memory_order_release);
  });

  while (!done.load(std::memory_order_acquire)) {
    const auto snapshot = statistics.snapshot();
    assert(snapshot.orders_executed == snapshot.quantity_executed);
    assert(snapshot.value_executed == snapshot.quantity_executed * 2);
    assert(snapshot.last_execution_timestamp == snapshot.orders_executed);
  }
  writer.join();
}

} // namespace

int main() {
  for (const std::uint64_t seed : {
         0xC0FFEEULL,
         0x5EED1234ULL,
         0xBAD5EEDULL
       }) {
    run_randomized_sequence(seed);
  }
  cancellation_and_matching_are_serialized();
  fok_and_post_only_decisions_are_serialized();
  statistics_snapshots_are_not_torn();
  return 0;
}
