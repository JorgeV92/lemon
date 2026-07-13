#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <lemon/lemon.hpp>

namespace {

constexpr lemon::TimestampMs timestamp = 1'717'286'400'000;

lemon::OrderType standard(std::uint64_t quantity = 10) {
  return lemon::OrderType::standard(
    1,
    lemon::Side::Buy,
    lemon::Price{100},
    lemon::Quantity{quantity},
    timestamp
  );
}

void factories_create_all_variants() {
  const auto standard_order = standard();
  assert(standard_order.kind() == lemon::OrderKind::Standard);
  assert(standard_order.get_visible_quantity() == lemon::Quantity{10});
  assert(standard_order.get_hidden_quantity() == lemon::Quantity::zero());

  const auto iceberg = lemon::OrderType::iceberg(
    2, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{4}, lemon::Quantity{6}, timestamp
  );
  assert(iceberg.kind() == lemon::OrderKind::Iceberg);
  assert(iceberg.get_quantity() == lemon::Quantity{10});
  assert(iceberg.get_if<lemon::IcebergOrder>() != nullptr);

  const auto post_only = lemon::OrderType::post_only(
    3, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{8}, timestamp
  );
  assert(post_only.kind() == lemon::OrderKind::PostOnly);
  assert(post_only.is_post_only());

  const auto trailing = lemon::OrderType::trailing_stop(
    4, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{7},
    lemon::Quantity{5}, lemon::Price{110}, timestamp
  );
  assert(trailing.kind() == lemon::OrderKind::TrailingStop);
  assert(trailing.get_if<lemon::TrailingStopOrder>()->trail_amount ==
         lemon::Quantity{5});

  const auto pegged = lemon::OrderType::pegged(
    5, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{6},
    -2, lemon::PegReferenceType::BestAsk, timestamp
  );
  assert(pegged.kind() == lemon::OrderKind::Pegged);
  assert(pegged.get_if<lemon::PeggedOrder>()->reference_price_offset == -2);

  const auto market_to_limit = lemon::OrderType::market_to_limit(
    6, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{5}, timestamp
  );
  assert(market_to_limit.kind() == lemon::OrderKind::MarketToLimit);

  const auto reserve = lemon::OrderType::reserve(
    7, lemon::Side::Buy, lemon::Price{100},
    lemon::Quantity{3}, lemon::Quantity{12}, lemon::Quantity{2},
    lemon::NonZeroQuantity{4}, true, timestamp
  );
  assert(reserve.kind() == lemon::OrderKind::Reserve);
  assert(reserve.get_quantity() == lemon::Quantity{15});
  assert(reserve.get_if<lemon::ReserveOrder>()->auto_replenish);
}

void legacy_constructor_infers_standard_or_iceberg() {
  const lemon::OrderType fully_visible{
    1, lemon::Side::Buy, lemon::Price{100},
    lemon::Quantity{10}, lemon::Quantity{10}, lemon::Quantity::zero(),
    timestamp
  };
  assert(fully_visible.kind() == lemon::OrderKind::Standard);

  const lemon::OrderType hidden{
    2, lemon::Side::Buy, lemon::Price{100},
    lemon::Quantity{10}, lemon::Quantity{7}, lemon::Quantity{3},
    timestamp
  };
  assert(hidden.kind() == lemon::OrderKind::Iceberg);
  assert(hidden.get_quantity() == lemon::Quantity{10});
}

void nonzero_replenishment_rejects_zero() {
  bool threw = false;
  try {
    static_cast<void>(lemon::NonZeroQuantity{0});
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
}

void matchability_accounts_for_hidden_depth() {
  assert(standard(1).is_matchable());
  assert(!standard(0).is_matchable());

  const auto hidden_iceberg = lemon::OrderType::iceberg(
    2, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity::zero(), lemon::Quantity{5}, timestamp
  );
  assert(hidden_iceberg.is_matchable());

  const auto automatic_reserve = lemon::OrderType::reserve(
    3, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity::zero(), lemon::Quantity{5}, lemon::Quantity{1},
    lemon::NonZeroQuantity{2}, true, timestamp
  );
  const auto manual_reserve = lemon::OrderType::reserve(
    4, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity::zero(), lemon::Quantity{5}, lemon::Quantity{1},
    lemon::NonZeroQuantity{2}, false, timestamp
  );
  assert(automatic_reserve.is_matchable());
  assert(!manual_reserve.is_matchable());
}

void reduced_quantity_preserves_variant_metadata() {
  const auto iceberg = lemon::OrderType::iceberg(
    1, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{5}, lemon::Quantity{9}, timestamp
  ).with_reduced_quantity(2);
  assert(iceberg.get_visible_quantity() == lemon::Quantity{2});
  assert(iceberg.get_hidden_quantity() == lemon::Quantity{9});

  const auto trailing = lemon::OrderType::trailing_stop(
    2, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{8},
    lemon::Quantity{3}, lemon::Price{105}, timestamp
  ).with_reduced_quantity(4);
  assert(trailing.get_visible_quantity() == lemon::Quantity{4});
  assert(trailing.get_if<lemon::TrailingStopOrder>()->trail_amount ==
         lemon::Quantity{3});

  const auto reserve = lemon::OrderType::reserve(
    3, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{6}, lemon::Quantity{10}, lemon::Quantity{2},
    lemon::NonZeroQuantity{3}, true, timestamp
  ).with_reduced_quantity(1);
  assert(reserve.get_visible_quantity() == lemon::Quantity{1});
  assert(reserve.get_hidden_quantity() == lemon::Quantity{10});
  assert(reserve.get_if<lemon::ReserveOrder>()->replenish_amount ==
         lemon::NonZeroQuantity{3});
}

void standard_matching_handles_full_partial_and_excess() {
  const auto order = standard(10);

  const auto partial = order.match_against(4);
  assert(partial.consumed == 4);
  assert(partial.hidden_reduced == 0);
  assert(partial.remaining_quantity == 0);
  assert(partial.updated_order.has_value());
  assert(partial.updated_order->get_visible_quantity() == lemon::Quantity{6});

  const auto full = order.match_against(10);
  assert(full.consumed == 10);
  assert(!full.updated_order.has_value());
  assert(full.remaining_quantity == 0);

  const auto excess = order.match_against(14);
  assert(excess.consumed == 10);
  assert(!excess.updated_order.has_value());
  assert(excess.remaining_quantity == 4);
}

void visible_variants_match_like_standard_resting_orders() {
  const std::vector<lemon::OrderType> orders{
    lemon::OrderType::post_only(
      1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp
    ),
    lemon::OrderType::trailing_stop(
      2, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10},
      lemon::Quantity{2}, lemon::Price{105}, timestamp
    ),
    lemon::OrderType::pegged(
      3, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10},
      1, lemon::PegReferenceType::BestBid, timestamp
    ),
    lemon::OrderType::market_to_limit(
      4, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{10}, timestamp
    )
  };

  for (const auto& order : orders) {
    const auto result = order.match_against(3);
    assert(result.consumed == 3);
    assert(result.hidden_reduced == 0);
    assert(result.remaining_quantity == 0);
    assert(result.updated_order.has_value());
    assert(result.updated_order->kind() == order.kind());
    assert(result.updated_order->get_visible_quantity() == lemon::Quantity{7});
  }
}

void iceberg_matching_replenishes_and_handles_zero_visible() {
  const auto order = lemon::OrderType::iceberg(
    1, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{5}, lemon::Quantity{12}, timestamp
  );

  const auto partial = order.match_against(3);
  assert(partial.consumed == 3);
  assert(partial.updated_order->get_visible_quantity() == lemon::Quantity{2});
  assert(partial.updated_order->get_hidden_quantity() == lemon::Quantity{12});

  const auto replenished = order.match_against(5);
  assert(replenished.consumed == 5);
  assert(replenished.hidden_reduced == 5);
  assert(replenished.updated_order->get_visible_quantity() == lemon::Quantity{5});
  assert(replenished.updated_order->get_hidden_quantity() == lemon::Quantity{7});

  const auto hidden_only = lemon::OrderType::iceberg(
    2, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity::zero(), lemon::Quantity{6}, timestamp
  ).match_against(2);
  assert(hidden_only.consumed == 0);
  assert(hidden_only.hidden_reduced == 6);
  assert(hidden_only.remaining_quantity == 2);
  assert(hidden_only.updated_order->get_visible_quantity() == lemon::Quantity{6});
  assert(hidden_only.updated_order->get_hidden_quantity() == lemon::Quantity::zero());
}

void reserve_matching_honors_threshold_and_auto_replenishment() {
  const auto order = lemon::OrderType::reserve(
    1, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{4}, lemon::Quantity{10}, lemon::Quantity{3},
    lemon::NonZeroQuantity{2}, true, timestamp
  );

  const auto threshold_replenish = order.match_against(2);
  assert(threshold_replenish.consumed == 2);
  assert(threshold_replenish.hidden_reduced == 2);
  assert(threshold_replenish.updated_order->get_visible_quantity() ==
         lemon::Quantity{4});
  assert(threshold_replenish.updated_order->get_hidden_quantity() ==
         lemon::Quantity{8});

  const auto full_visible = order.match_against(4);
  assert(full_visible.consumed == 4);
  assert(full_visible.hidden_reduced == 2);
  assert(full_visible.updated_order->get_visible_quantity() == lemon::Quantity{2});
  assert(full_visible.updated_order->get_hidden_quantity() == lemon::Quantity{8});

  const auto manual = lemon::OrderType::reserve(
    2, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{4}, lemon::Quantity{10}, lemon::Quantity{3},
    lemon::NonZeroQuantity{2}, false, timestamp
  ).match_against(4);
  assert(manual.consumed == 4);
  assert(manual.hidden_reduced == 0);
  assert(!manual.updated_order.has_value());

  const auto default_replenish = lemon::OrderType::reserve(
    3, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{1}, lemon::Quantity{100}, lemon::Quantity{1},
    std::nullopt, true, timestamp
  ).match_against(1);
  assert(default_replenish.hidden_reduced == 80);
  assert(default_replenish.updated_order->get_visible_quantity() ==
         lemon::Quantity{80});
  assert(default_replenish.updated_order->get_hidden_quantity() ==
         lemon::Quantity{20});
}

void explicit_refresh_draws_only_available_hidden_quantity() {
  const auto iceberg = lemon::OrderType::iceberg(
    1, lemon::Side::Sell, lemon::Price{100},
    lemon::Quantity{7}, lemon::Quantity{3}, timestamp
  );
  const auto [refreshed, used] = iceberg.refresh_iceberg(
    lemon::NonZeroQuantity{2}
  );
  assert(used == 2);
  assert(refreshed.get_visible_quantity() == lemon::Quantity{2});
  assert(refreshed.get_hidden_quantity() == lemon::Quantity{1});

  const auto [unchanged, standard_used] = standard().refresh_iceberg(
    lemon::NonZeroQuantity{2}
  );
  assert(standard_used == 0);
  assert(unchanged.get_visible_quantity() == lemon::Quantity{10});
}

void snapshot_checksum_payload_contains_variant_state() {
  auto trailing = std::make_shared<lemon::OrderType>(
    lemon::OrderType::trailing_stop(
      1, lemon::Side::Sell, lemon::Price{100}, lemon::Quantity{7},
      lemon::Quantity{5}, lemon::Price{110}, timestamp
    )
  );
  auto pegged = std::make_shared<lemon::OrderType>(
    lemon::OrderType::pegged(
      2, lemon::Side::Buy, lemon::Price{100}, lemon::Quantity{6},
      -2, lemon::PegReferenceType::BestAsk, timestamp
    )
  );
  auto reserve = std::make_shared<lemon::OrderType>(
    lemon::OrderType::reserve(
      3, lemon::Side::Buy, lemon::Price{100},
      lemon::Quantity{3}, lemon::Quantity{12}, lemon::Quantity{2},
      lemon::NonZeroQuantity{4}, true, timestamp
    )
  );

  const lemon::PriceLevelSnapshotPackage package{
    lemon::PriceLevelSnapshot::with_orders(
      lemon::Price{100},
      {trailing, pegged, reserve}
    )
  };
  const std::string json = package.to_json_string();

  assert(json.find("\"kind\":\"trailing_stop\"") != std::string::npos);
  assert(json.find("\"trail_amount\":5") != std::string::npos);
  assert(json.find("\"reference_price_offset\":-2") != std::string::npos);
  assert(json.find("\"kind\":\"reserve\"") != std::string::npos);
  assert(json.find("\"replenish_amount\":4") != std::string::npos);
  assert(json.find("\"auto_replenish\":true") != std::string::npos);
}

} // namespace

int main() {
  factories_create_all_variants();
  legacy_constructor_infers_standard_or_iceberg();
  nonzero_replenishment_rejects_zero();
  matchability_accounts_for_hidden_depth();
  reduced_quantity_preserves_variant_metadata();
  standard_matching_handles_full_partial_and_excess();
  visible_variants_match_like_standard_resting_orders();
  iceberg_matching_replenishes_and_handles_zero_visible();
  reserve_matching_honors_threshold_and_auto_replenishment();
  explicit_refresh_draws_only_available_hidden_quantity();
  snapshot_checksum_payload_contains_variant_state();
  return 0;
}
