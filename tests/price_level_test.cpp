#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
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

std::vector<lemon::OrderId> order_ids(
  const std::vector<std::shared_ptr<lemon::OrderType>>& orders
) {
  std::vector<lemon::OrderId> result;
  for (const auto& order : orders) {
    result.push_back(order->get_id());
  }
  return result;
}

void add_order_updates_state_and_statistics() {
  lemon::PriceLevel level{lemon::Price{100}};
  const auto stored = level.add_order(maker(1, 10, 100));

  assert(stored->get_id() == 1);
  assert(level.visible_quantity() == lemon::Quantity{10});
  assert(level.hidden_quantity() == lemon::Quantity::zero());
  assert(level.total_quantity() == lemon::Quantity{10});
  assert(level.order_count() == 1);
  assert(level.stats()->orders_added() == 1);
  assert(level.iter_orders().size() == 1);

  bool duplicate_rejected = false;
  try {
    static_cast<void>(level.add_order(maker(1, 5, 101)));
  } catch (const lemon::PriceLevelError&) {
    duplicate_rejected = true;
  }
  assert(duplicate_rejected);
  assert(level.order_count() == 1);
}

void standard_matching_preserves_fifo_and_builds_trades() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 10, 100));
  level.add_order(maker(2, 10, 101));
  lemon::TradeIdGenerator trade_ids{1000};

  const auto first = level.match_order(
    lemon::Quantity{4},
    90,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  );

  assert(first.is_complete());
  assert(first.outcome() == lemon::MatchOutcome::Filled);
  assert(first.remaining_quantity() == lemon::Quantity::zero());
  assert(first.trades().size() == 1);
  const auto& trade = first.trades().as_vector().front();
  assert(trade.trade_id() == 1000);
  assert(trade.taker_order_id() == 90);
  assert(trade.maker_order_id() == 1);
  assert(trade.price() == lemon::Price{100});
  assert(trade.quantity() == lemon::Quantity{4});
  assert(trade.taker_side() == lemon::Side::Buy);
  assert(trade.timestamp() == 200);

  auto orders = level.snapshot_by_insertion_sequence();
  assert(order_ids(orders) == (std::vector<lemon::OrderId>{1, 2}));
  assert(orders[0]->get_visible_quantity() == lemon::Quantity{6});
  assert(level.visible_quantity() == lemon::Quantity{16});
  assert(level.order_count() == 2);

  const auto second = level.match_order(
    lemon::Quantity{8},
    91,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    201,
    trade_ids
  );
  assert(second.trades().size() == 2);
  assert(second.filled_order_ids() == (std::vector<lemon::OrderId>{1}));
  assert(second.executed_quantity() == lemon::Quantity{8});
  assert(second.executed_value() == 800);
  assert(second.average_price() == 100.0);

  orders = level.snapshot_by_insertion_sequence();
  assert(order_ids(orders) == (std::vector<lemon::OrderId>{2}));
  assert(orders[0]->get_visible_quantity() == lemon::Quantity{8});
  assert(level.visible_quantity() == lemon::Quantity{8});
  assert(level.order_count() == 1);

  assert(level.stats()->orders_executed() == 3);
  assert(level.stats()->quantity_executed() == 12);
  assert(level.stats()->value_executed() == 1200);
  assert(level.stats()->last_execution_timestamp() == 201);
}

void replenished_iceberg_loses_priority() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(lemon::OrderType::iceberg(
    1,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity{3},
    lemon::Quantity{6},
    100
  ));
  level.add_order(maker(2, 10, 101));
  lemon::TradeIdGenerator trade_ids;

  const auto result = level.match_order(
    lemon::Quantity{3},
    90,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  );
  assert(result.is_complete());
  assert(result.trades().size() == 1);

  const auto orders = level.snapshot_by_insertion_sequence();
  assert(order_ids(orders) == (std::vector<lemon::OrderId>{2, 1}));
  assert(orders[1]->get_visible_quantity() == lemon::Quantity{3});
  assert(orders[1]->get_hidden_quantity() == lemon::Quantity{3});
  assert(level.visible_quantity() == lemon::Quantity{13});
  assert(level.hidden_quantity() == lemon::Quantity{3});
}

void taker_policies_leave_or_consume_liquidity_correctly() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 5, 100));
  lemon::TradeIdGenerator trade_ids;

  const auto rejected = level.match_order(
    lemon::Quantity{1},
    90,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::PostOnly,
    200,
    trade_ids
  );
  assert(rejected.was_rejected());
  assert(rejected.trades().empty());
  assert(level.visible_quantity() == lemon::Quantity{5});

  const auto killed = level.match_order(
    lemon::Quantity{6},
    91,
    lemon::TimeInForcePolicy{lemon::TimeInForce::FillOrKill},
    lemon::TakerKind::Standard,
    201,
    trade_ids
  );
  assert(killed.was_killed());
  assert(killed.remaining_quantity() == lemon::Quantity{6});
  assert(level.visible_quantity() == lemon::Quantity{5});

  const auto ioc = level.match_order(
    lemon::Quantity{7},
    92,
    lemon::TimeInForcePolicy{lemon::TimeInForce::ImmediateOrCancel},
    lemon::TakerKind::Standard,
    202,
    trade_ids
  );
  assert(ioc.outcome() == lemon::MatchOutcome::PartiallyFilled);
  assert(ioc.executed_quantity() == lemon::Quantity{5});
  assert(ioc.remaining_quantity() == lemon::Quantity{2});
  assert(level.order_count() == 0);
}

void fill_or_kill_counts_hidden_iceberg_depth() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(lemon::OrderType::iceberg(
    1,
    lemon::Side::Sell,
    lemon::Price{100},
    lemon::Quantity::zero(),
    lemon::Quantity{6},
    100
  ));
  lemon::TradeIdGenerator trade_ids;

  assert(level.matchable_quantity(lemon::Quantity{4}, 90) ==
         lemon::Quantity{4});
  const auto result = level.match_order(
    lemon::Quantity{4},
    90,
    lemon::TimeInForcePolicy{lemon::TimeInForce::FillOrKill},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  );
  assert(result.is_complete());
  assert(result.executed_quantity() == lemon::Quantity{4});
  const auto orders = level.snapshot_by_insertion_sequence();
  assert(orders.size() == 1);
  assert(orders[0]->get_visible_quantity() == lemon::Quantity{2});
  assert(orders[0]->get_hidden_quantity() == lemon::Quantity::zero());
}

void updates_follow_price_time_priority_rules() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 10, 100));
  level.add_order(maker(2, 10, 101));

  const auto reduced = level.update_order(
    lemon::OrderUpdate::quantity(1, lemon::Quantity{5})
  );
  assert(reduced.has_value());
  assert(order_ids(level.snapshot_by_insertion_sequence()) ==
         (std::vector<lemon::OrderId>{1, 2}));

  const auto increased = level.update_order(
    lemon::OrderUpdate::quantity(1, lemon::Quantity{15})
  );
  assert(increased.has_value());
  assert(order_ids(level.snapshot_by_insertion_sequence()) ==
         (std::vector<lemon::OrderId>{2, 1}));
  assert(level.visible_quantity() == lemon::Quantity{25});

  const auto cancelled = level.update_order(lemon::OrderUpdate::cancel(2));
  assert(cancelled.has_value());
  assert((*cancelled)->get_id() == 2);
  assert(level.order_count() == 1);
  assert(level.visible_quantity() == lemon::Quantity{15});

  bool same_price_rejected = false;
  try {
    static_cast<void>(level.update_order(
      lemon::OrderUpdate::price(1, lemon::Price{100})
    ));
  } catch (const lemon::PriceLevelError&) {
    same_price_rejected = true;
  }
  assert(same_price_rejected);

  const auto moved = level.update_order(
    lemon::OrderUpdate::price(1, lemon::Price{101})
  );
  assert(moved.has_value());
  assert(level.order_count() == 0);
  assert(level.stats()->orders_removed() == 2);
}

void snapshot_json_roundtrip_validates_checksum_and_restores_statistics() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 10, 100));
  lemon::TradeIdGenerator trade_ids;
  static_cast<void>(level.match_order(
    lemon::Quantity{4},
    90,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  ));

  const std::string json = level.snapshot_to_json();
  assert(json.find("\"version\":2") != std::string::npos);
  assert(json.find("\"statistics\"") != std::string::npos);

  const lemon::PriceLevel restored = lemon::PriceLevel::from_snapshot_json(json);
  assert(restored == level);
  assert(restored.visible_quantity() == lemon::Quantity{6});
  assert(restored.order_count() == 1);
  assert(restored.stats()->orders_added() == 1);
  assert(restored.stats()->orders_executed() == 1);
  assert(restored.stats()->quantity_executed() == 4);
  assert(restored.stats()->last_execution_timestamp() == 200);

  std::string bad_checksum = json;
  const std::string marker = "\"checksum\":\"";
  const std::size_t checksum_position = bad_checksum.find(marker) + marker.size();
  bad_checksum[checksum_position] =
    bad_checksum[checksum_position] == '0' ? '1' : '0';

  bool checksum_rejected = false;
  try {
    static_cast<void>(lemon::PriceLevel::from_snapshot_json(bad_checksum));
  } catch (const lemon::PriceLevelError&) {
    checksum_rejected = true;
  }
  assert(checksum_rejected);

  std::string bad_version = json;
  bad_version.replace(
    bad_version.find("\"version\":2"),
    std::string{"\"version\":2"}.size(),
    "\"version\":1"
  );
  bool version_rejected = false;
  try {
    static_cast<void>(lemon::PriceLevel::from_snapshot_json(bad_version));
  } catch (const lemon::PriceLevelError&) {
    version_rejected = true;
  }
  assert(version_rejected);

  std::string out_of_range_version = json;
  out_of_range_version.replace(
    out_of_range_version.find("\"version\":2"),
    std::string{"\"version\":2"}.size(),
    "\"version\":4294967298"
  );
  bool range_rejected = false;
  try {
    static_cast<void>(
      lemon::PriceLevel::from_snapshot_json(out_of_range_version)
    );
  } catch (const lemon::PriceLevelError&) {
    range_rejected = true;
  }
  assert(range_rejected);
}

void snapshot_json_roundtrips_every_order_variant() {
  constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;
  lemon::UserHash user_id{};
  user_id[0] = 0xab;
  user_id[31] = 0xcd;

  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(lemon::OrderType::standard(
    1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{1}, timestamp,
    {}, std::nullopt, user_id
  ));
  level.add_order(lemon::OrderType::iceberg(
    2, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{2}, lemon::Quantity{20}, timestamp + 1
  ));
  level.add_order(lemon::OrderType::post_only(
    3, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{3}, timestamp + 2
  ));
  level.add_order(lemon::OrderType::trailing_stop(
    4, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{4},
    lemon::Quantity{7}, lemon::Price{111}, timestamp + 3
  ));
  level.add_order(lemon::OrderType::pegged(
    5, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{5},
    -2, lemon::PegReferenceType::BestAsk, timestamp + 4
  ));
  level.add_order(lemon::OrderType::market_to_limit(
    6, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{6}, timestamp + 5
  ));
  level.add_order(lemon::OrderType::reserve(
    7, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{7}, lemon::Quantity{70}, lemon::Quantity{3},
    lemon::NonZeroQuantity{9}, true, timestamp + 6,
    lemon::TimeInForcePolicy{
      lemon::TimeInForce::GoodTillDate,
      timestamp + 10'000
    }
  ));

  const auto restored = lemon::PriceLevel::from_snapshot_json(
    level.snapshot_to_json()
  );
  const auto orders = restored.snapshot_by_insertion_sequence();
  assert(orders.size() == 7);
  for (std::size_t index = 0; index < orders.size(); ++index) {
    assert(orders[index]->kind() == static_cast<lemon::OrderKind>(index));
  }
  assert(orders[0]->get_user_id() == user_id);
  assert(orders[3]->get_if<lemon::TrailingStopOrder>()->trail_amount ==
         lemon::Quantity{7});
  assert(orders[4]->get_if<lemon::PeggedOrder>()->reference_price_offset == -2);
  const auto* reserve = orders[6]->get_if<lemon::ReserveOrder>();
  assert(reserve != nullptr);
  assert(reserve->replenish_threshold == lemon::Quantity{3});
  assert(reserve->replenish_amount == lemon::NonZeroQuantity{9});
  assert(reserve->auto_replenish);
  assert(orders[6]->get_time_in_force().type ==
         lemon::TimeInForce::GoodTillDate);
  assert(orders[6]->get_time_in_force().expiry_timestamp ==
         timestamp + 10'000);
}

void snapshot_recovery_rejects_duplicate_order_ids() {
  auto first = std::make_shared<lemon::OrderType>(maker(1, 3, 100));
  auto duplicate = std::make_shared<lemon::OrderType>(maker(1, 4, 101));
  bool rejected = false;
  try {
    static_cast<void>(lemon::PriceLevel::from_snapshot(
      lemon::PriceLevelSnapshot::with_orders(
        lemon::Price{100},
        {first, duplicate}
      )
    ));
  } catch (const lemon::PriceLevelError&) {
    rejected = true;
  }
  assert(rejected);
}

void data_and_comparison_use_price_level_state() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 3, 100));
  const lemon::PriceLevelData data = level.data();
  const lemon::PriceLevel restored = lemon::PriceLevel::from_data(data);
  const lemon::PriceLevel higher{lemon::Price{101}};

  assert(restored.price() == lemon::Price{100});
  assert(restored.visible_quantity() == lemon::Quantity{3});
  assert(restored.stats()->orders_added() == 1);
  assert(level == restored);
  assert(level < higher);
}

void self_match_is_rejected_without_mutation() {
  const std::vector policies{
    std::pair{
      lemon::TimeInForcePolicy{},
      lemon::TakerKind::Standard
    },
    std::pair{
      lemon::TimeInForcePolicy{lemon::TimeInForce::FillOrKill},
      lemon::TakerKind::Standard
    },
    std::pair{
      lemon::TimeInForcePolicy{lemon::TimeInForce::ImmediateOrCancel},
      lemon::TakerKind::Standard
    },
    std::pair{
      lemon::TimeInForcePolicy{},
      lemon::TakerKind::PostOnly
    }
  };

  for (const auto& [time_in_force, taker_kind] : policies) {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(maker(1, 4, 100));
    level.add_order(maker(2, 6, 101));
    const std::string before = level.snapshot_to_json();
    const auto statistics_before = level.stats()->snapshot();
    lemon::TradeIdGenerator trade_ids{50};

    const auto result = level.match_order(
      lemon::Quantity{7},
      1,
      time_in_force,
      taker_kind,
      200,
      trade_ids
    );

    assert(result.was_rejected());
    assert(!result.was_killed());
    assert(result.trades().empty());
    assert(result.remaining_quantity() == lemon::Quantity{7});
    assert(level.snapshot_to_json() == before);
    assert(level.stats()->snapshot().orders_executed ==
           statistics_before.orders_executed);
    assert(level.stats()->snapshot().quantity_executed ==
           statistics_before.quantity_executed);
  }
}

void self_match_behind_another_maker_is_still_terminal() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(2, 6, 100));
  level.add_order(maker(1, 4, 101));
  const std::string before = level.snapshot_to_json();
  lemon::TradeIdGenerator trade_ids{50};

  const auto result = level.match_order(
    lemon::Quantity{6},
    1,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  );

  assert(result.was_rejected());
  assert(result.trades().empty());
  assert(level.snapshot_to_json() == before);
}

void matchable_quantity_skips_self_and_other_ids_match_normally() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 4, 100));
  level.add_order(maker(2, 6, 101));

  assert(level.matchable_quantity(lemon::Quantity{10}, 1) ==
         lemon::Quantity{6});
  assert(level.matchable_quantity(lemon::Quantity{10}, 99) ==
         lemon::Quantity{10});

  lemon::TradeIdGenerator trade_ids{50};
  const auto result = level.match_order(
    lemon::Quantity{4},
    99,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    200,
    trade_ids
  );
  assert(result.is_complete());
  assert(result.trades().size() == 1);
  assert(result.trades().as_vector().front().maker_order_id() == 1);
}

void snapshot_recovery_preserves_exact_consumption_order() {
  lemon::PriceLevel level{lemon::Price{100}};
  level.add_order(maker(1, 5, 300));
  level.add_order(maker(2, 5, 100));
  level.add_order(maker(3, 5, 100));

  // Multiple increases demote makers while their client timestamps remain
  // unchanged. The resulting consumption order is C, A, B.
  assert(level.update_order(
    lemon::OrderUpdate::quantity(1, lemon::Quantity{8})
  ));
  assert(level.update_order(
    lemon::OrderUpdate::quantity(2, lemon::Quantity{9})
  ));
  const std::vector<lemon::OrderId> expected{3, 1, 2};
  assert(order_ids(level.snapshot_by_insertion_sequence()) == expected);

  const auto snapshot = level.snapshot();
  assert(order_ids(snapshot.orders()) == expected);
  const lemon::PriceLevelSnapshotPackage package{snapshot};
  package.validate();

  const auto from_snapshot = lemon::PriceLevel::from_snapshot(snapshot);
  auto from_json = lemon::PriceLevel::from_snapshot_json(
    level.snapshot_to_json()
  );
  const auto from_data = lemon::PriceLevel::from_data(level.data());
  assert(order_ids(from_snapshot.snapshot_by_insertion_sequence()) == expected);
  assert(order_ids(from_json.snapshot_by_insertion_sequence()) == expected);
  assert(order_ids(from_data.snapshot_by_insertion_sequence()) == expected);
  assert(from_json.visible_quantity() == level.visible_quantity());
  assert(from_json.hidden_quantity() == level.hidden_quantity());
  assert(from_json.order_count() == level.order_count());

  lemon::TradeIdGenerator trade_ids{70};
  const auto result = from_json.match_order(
    lemon::Quantity{1},
    99,
    lemon::TimeInForcePolicy{},
    lemon::TakerKind::Standard,
    400,
    trade_ids
  );
  assert(result.trades().as_vector().front().maker_order_id() == 3);
}

void snapshot_recovery_preserves_replenishment_demotions() {
  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(lemon::OrderType::iceberg(
      1, lemon::Side::Sell, lemon::Price{100},
      lemon::Quantity{3}, lemon::Quantity{3}, 100
    ));
    level.add_order(maker(2, 4, 200));
    lemon::TradeIdGenerator trade_ids{80};
    static_cast<void>(level.match_order(
      lemon::Quantity{3}, 90, {}, lemon::TakerKind::Standard, 300, trade_ids
    ));
    const std::vector<lemon::OrderId> expected{2, 1};
    assert(order_ids(level.snapshot_by_insertion_sequence()) == expected);
    const auto restored = lemon::PriceLevel::from_snapshot_json(
      level.snapshot_to_json()
    );
    assert(order_ids(restored.snapshot_by_insertion_sequence()) == expected);
  }

  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(lemon::OrderType::reserve(
      1, lemon::Side::Sell, lemon::Price{100},
      lemon::Quantity{3}, lemon::Quantity{4}, lemon::Quantity{2},
      lemon::NonZeroQuantity{2}, true, 100
    ));
    level.add_order(maker(2, 4, 200));
    lemon::TradeIdGenerator trade_ids{90};
    static_cast<void>(level.match_order(
      lemon::Quantity{3}, 91, {}, lemon::TakerKind::Standard, 300, trade_ids
    ));
    const std::vector<lemon::OrderId> expected{2, 1};
    assert(order_ids(level.snapshot_by_insertion_sequence()) == expected);
    const auto restored = lemon::PriceLevel::from_data(level.data());
    assert(order_ids(restored.snapshot_by_insertion_sequence()) == expected);
    assert(restored.visible_quantity() == level.visible_quantity());
    assert(restored.hidden_quantity() == level.hidden_quantity());
  }
}

void admission_enforces_price_and_single_side_topology() {
  lemon::PriceLevel level{lemon::Price{100}};
  const auto before = level.stats()->snapshot();

  bool wrong_price_rejected = false;
  try {
    level.add_order(lemon::OrderType::standard(
      1, lemon::Side::Sell, lemon::Price{101}, lemon::Quantity{2}, 100
    ));
  } catch (const lemon::PriceLevelError&) {
    wrong_price_rejected = true;
  }
  assert(wrong_price_rejected);
  assert(level.order_count() == 0);
  assert(level.stats()->snapshot().orders_added == before.orders_added);

  level.add_order(maker(1, 2, 100));
  level.add_order(maker(2, 2, 101));
  const std::string pinned = level.snapshot_to_json();

  bool opposite_side_rejected = false;
  try {
    level.add_order(lemon::OrderType::standard(
      3, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{2}, 102
    ));
  } catch (const lemon::PriceLevelError&) {
    opposite_side_rejected = true;
  }
  assert(opposite_side_rejected);
  assert(level.snapshot_to_json() == pinned);
}

void draining_or_removing_final_order_unpins_the_side() {
  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(maker(1, 2, 100));
    lemon::TradeIdGenerator trade_ids{100};
    const auto result = level.match_order(
      lemon::Quantity{2}, 99, {}, lemon::TakerKind::Standard, 200, trade_ids
    );
    assert(result.is_complete());
    assert(level.order_count() == 0);
    level.add_order(lemon::OrderType::standard(
      2, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{2}, 201
    ));
    assert(level.order_count() == 1);
  }

  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(maker(1, 2, 100));
    assert(level.update_order(lemon::OrderUpdate::cancel(1)));
    level.add_order(lemon::OrderType::standard(
      2, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{2}, 201
    ));
    assert(level.order_count() == 1);
  }

  {
    lemon::PriceLevel level{lemon::Price{100}};
    level.add_order(maker(1, 2, 100));
    assert(level.update_order(
      lemon::OrderUpdate::price(1, lemon::Price{101})
    ));
    level.add_order(lemon::OrderType::standard(
      2, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{2}, 201
    ));
    assert(level.order_count() == 1);
  }
}

void snapshot_recovery_rejects_invalid_topology() {
  const auto wrong_price = std::make_shared<lemon::OrderType>(
    lemon::OrderType::standard(
      1, lemon::Side::Sell, lemon::Price{101}, lemon::Quantity{2}, 100
    )
  );
  const auto sell = std::make_shared<lemon::OrderType>(maker(1, 2, 100));
  const auto buy = std::make_shared<lemon::OrderType>(
    lemon::OrderType::standard(
      2, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{2}, 101
    )
  );

  bool wrong_price_rejected = false;
  try {
    static_cast<void>(lemon::PriceLevel::from_snapshot(
      lemon::PriceLevelSnapshot::with_orders(
        lemon::Price{100}, {wrong_price}
      )
    ));
  } catch (const lemon::PriceLevelError&) {
    wrong_price_rejected = true;
  }
  assert(wrong_price_rejected);

  bool mixed_side_rejected = false;
  try {
    static_cast<void>(lemon::PriceLevel::from_snapshot(
      lemon::PriceLevelSnapshot::with_orders(
        lemon::Price{100}, {sell, buy}
      )
    ));
  } catch (const lemon::PriceLevelError&) {
    mixed_side_rejected = true;
  }
  assert(mixed_side_rejected);
}

} // namespace

int main() {
  add_order_updates_state_and_statistics();
  standard_matching_preserves_fifo_and_builds_trades();
  replenished_iceberg_loses_priority();
  taker_policies_leave_or_consume_liquidity_correctly();
  fill_or_kill_counts_hidden_iceberg_depth();
  updates_follow_price_time_priority_rules();
  snapshot_json_roundtrip_validates_checksum_and_restores_statistics();
  snapshot_json_roundtrips_every_order_variant();
  snapshot_recovery_rejects_duplicate_order_ids();
  data_and_comparison_use_price_level_state();
  self_match_is_rejected_without_mutation();
  self_match_behind_another_maker_is_still_terminal();
  matchable_quantity_skips_self_and_other_ids_match_normally();
  snapshot_recovery_preserves_exact_consumption_order();
  snapshot_recovery_preserves_replenishment_demotions();
  admission_enforces_price_and_single_side_topology();
  draining_or_removing_final_order_unpins_the_side();
  snapshot_recovery_rejects_invalid_topology();
  return 0;
}
