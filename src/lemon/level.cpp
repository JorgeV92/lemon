#include <algorithm>
#include <cassert>
#include <deque>
#include <limits>
#include <memory>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <utility>

#include <lemon/detail/pricelevel/level.hpp>

namespace lemon {

namespace {

std::uint64_t checked_total(const OrderType& order) {
  const std::uint64_t visible = order.get_visible_quantity().value();
  const std::uint64_t hidden = order.get_hidden_quantity().value();
  if (hidden > std::numeric_limits<std::uint64_t>::max() - visible) {
    throw PriceLevelError("order total quantity overflow");
  }
  return visible + hidden;
}

TakerKind taker_kind_for(const OrderType& order) {
  if (order.kind() == OrderKind::PostOnly) {
    return TakerKind::PostOnly;
  }
  if (order.kind() == OrderKind::MarketToLimit) {
    return TakerKind::MarketToLimit;
  }
  return TakerKind::Standard;
}

std::optional<Side> validate_topology(
  Price price,
  const std::vector<std::shared_ptr<OrderType>>& orders
) {
  std::optional<Side> side;
  std::unordered_set<OrderId> order_ids;
  order_ids.reserve(orders.size());

  for (const auto& order : orders) {
    if (!order) {
      throw PriceLevelError("snapshot contains a null order");
    }
    if (order->get_price() != price) {
      throw PriceLevelError("snapshot contains an order at the wrong price");
    }
    if (side && order->get_side() != *side) {
      throw PriceLevelError("snapshot contains mixed order sides");
    }
    if (!order_ids.insert(order->get_id()).second) {
      throw PriceLevelError("snapshot contains duplicate order IDs");
    }
    side = order->get_side();
  }

  return side;
}

} // namespace

PriceLevel::PriceLevel(Price price)
  : price_(price),
    stats_(std::make_shared<PriceLevelStatistics>()) {
}

PriceLevel::PriceLevel(
  Price price,
  Quantity visible_quantity,
  Quantity hidden_quantity,
  std::size_t order_count,
  std::vector<std::shared_ptr<OrderType>> orders,
  PriceLevelStatisticsData statistics,
  std::optional<Side> resting_side
)
  : price_(price),
    visible_quantity_(visible_quantity.value()),
    hidden_quantity_(hidden_quantity.value()),
    order_count_(order_count),
    orders_(orders),
    stats_(std::make_shared<PriceLevelStatistics>(statistics)),
    resting_side_(resting_side) {
}

PriceLevel PriceLevel::from_snapshot(PriceLevelSnapshot snapshot) {
  const Price price = snapshot.price();
  const std::optional<Side> side = validate_topology(
    price,
    snapshot.orders()
  );
  snapshot.refresh_aggregates();

  const Quantity visible = snapshot.visible_quantity();
  const Quantity hidden = snapshot.hidden_quantity();
  const std::size_t count = snapshot.order_count();
  const PriceLevelStatisticsData statistics = snapshot.statistics();
  auto orders = std::move(snapshot).into_orders();

  return PriceLevel{
    price,
    visible,
    hidden,
    count,
    std::move(orders),
    statistics,
    side
  };
}

PriceLevel PriceLevel::from_snapshot_package(
  PriceLevelSnapshotPackage package
) {
  return from_snapshot(std::move(package).into_snapshot());
}

PriceLevel PriceLevel::from_snapshot_json(std::string_view data) {
  return from_snapshot_package(
    PriceLevelSnapshotPackage::from_json_string(data)
  );
}

PriceLevel PriceLevel::from_data(const PriceLevelData& data) {
  PriceLevelStatisticsData statistics = PriceLevelStatistics{}.snapshot();
  statistics.orders_added = static_cast<std::uint64_t>(
    std::count_if(
      data.orders.begin(),
      data.orders.end(),
      [](const auto& order) { return static_cast<bool>(order); }
    )
  );
  return from_snapshot(
    PriceLevelSnapshot::with_orders_and_statistics(
      data.price,
      data.orders,
      statistics
    )
  );
}

Price PriceLevel::price() const {
  return price_;
}

Quantity PriceLevel::visible_quantity() const {
  return Quantity{visible_quantity_.load(std::memory_order_relaxed)};
}

Quantity PriceLevel::hidden_quantity() const {
  return Quantity{hidden_quantity_.load(std::memory_order_relaxed)};
}

Quantity PriceLevel::total_quantity() const {
  const std::uint64_t visible = visible_quantity().value();
  const std::uint64_t hidden = hidden_quantity().value();
  if (hidden > std::numeric_limits<std::uint64_t>::max() - visible) {
    throw PriceLevelError("price level total quantity overflow");
  }
  return Quantity{visible + hidden};
}

std::size_t PriceLevel::order_count() const {
  return order_count_.load(std::memory_order_relaxed);
}

std::shared_ptr<PriceLevelStatistics> PriceLevel::stats() const {
  return stats_;
}

std::shared_ptr<OrderType> PriceLevel::add_order(OrderType order) {
  std::lock_guard<std::mutex> match_lock(match_mutex_);
  if (order.get_price() != price_) {
    throw PriceLevelError("order price does not match the price level");
  }
  if (resting_side_ && order.get_side() != *resting_side_) {
    throw PriceLevelError("order side does not match the resting level side");
  }
  if (orders_.find(order.get_id())) {
    throw PriceLevelError("an order with this ID already exists at the level");
  }

  const std::uint64_t visible = order.get_visible_quantity().value();
  const std::uint64_t hidden = order.get_hidden_quantity().value();
  const std::uint64_t current_visible = visible_quantity_.load(std::memory_order_relaxed);
  const std::uint64_t current_hidden = hidden_quantity_.load(std::memory_order_relaxed);
  const std::size_t current_count = order_count_.load(std::memory_order_relaxed);

  if (visible > std::numeric_limits<std::uint64_t>::max() - current_visible) {
    throw PriceLevelError("visible quantity overflow while adding order");
  }
  if (hidden > std::numeric_limits<std::uint64_t>::max() - current_hidden) {
    throw PriceLevelError("hidden quantity overflow while adding order");
  }
  if (current_count == std::numeric_limits<std::size_t>::max()) {
    throw PriceLevelError("order count overflow while adding order");
  }

  auto stored = std::make_shared<OrderType>(std::move(order));
  orders_.push(stored);
  visible_quantity_.store(current_visible + visible, std::memory_order_relaxed);
  hidden_quantity_.store(current_hidden + hidden, std::memory_order_relaxed);
  order_count_.store(current_count + 1, std::memory_order_relaxed);
  if (current_count == 0) {
    resting_side_ = stored->get_side();
  }
  stats_->record_order_added();
  return stored;
}

std::vector<std::shared_ptr<OrderType>> PriceLevel::iter_orders() const {
  return orders_.snapshot_by_sequence();
}

std::vector<std::shared_ptr<OrderType>> PriceLevel::snapshot_orders() const {
  return orders_.snapshot_vec();
}

std::vector<std::shared_ptr<OrderType>>
PriceLevel::snapshot_by_insertion_sequence() const {
  return orders_.snapshot_by_sequence();
}

void PriceLevel::snapshot_by_sequence_into(
  std::vector<std::shared_ptr<OrderType>>& result
) const {
  orders_.snapshot_by_sequence_into(result);
}

bool PriceLevel::has_matchable_depth() const {
  for (const auto& order : orders_.snapshot_by_sequence()) {
    if (order && order->is_matchable()) {
      return true;
    }
  }
  return false;
}

Quantity PriceLevel::matchable_quantity(
  Quantity incoming_quantity,
  OrderId taker_order_id
) const {
  if (incoming_quantity == Quantity::zero()) {
    return Quantity::zero();
  }

  std::deque<std::shared_ptr<OrderType>> pending;
  for (auto& order : orders_.snapshot_by_sequence()) {
    pending.push_back(std::move(order));
  }

  std::uint64_t remaining = incoming_quantity.value();
  std::uint64_t filled = 0;

  while (remaining > 0 && !pending.empty()) {
    auto order = std::move(pending.front());
    pending.pop_front();
    if (!order) {
      continue;
    }
    if (order->get_id() == taker_order_id) {
      continue;
    }

    OrderMatchResult match = order->match_against(remaining);
    if (match.consumed == 0 &&
        match.hidden_reduced == 0 &&
        match.remaining_quantity == remaining &&
        match.updated_order.has_value()) {
      continue;
    }

    if (match.consumed > std::numeric_limits<std::uint64_t>::max() - filled) {
      throw PriceLevelError("matchable quantity overflow");
    }
    filled += match.consumed;
    remaining = match.remaining_quantity;

    if (match.updated_order) {
      auto updated = std::make_shared<OrderType>(*match.updated_order);
      if (match.hidden_reduced > 0) {
        pending.push_back(std::move(updated));
      } else {
        pending.push_front(std::move(updated));
      }
    }
  }

  return Quantity{filled};
}

MatchResult PriceLevel::match_order(
  Quantity incoming_quantity,
  OrderId taker_order_id,
  TimeInForcePolicy taker_time_in_force,
  TakerKind taker_kind,
  TimestampMs timestamp,
  TradeIdGenerator& trade_id_generator
) {
  std::lock_guard<std::mutex> match_lock(match_mutex_);

  // Self-match rejection is terminal and takes precedence over taker policy.
  // The level mutex makes the lookup and decision one atomic operation.
  if (incoming_quantity.value() > 0 && orders_.find(taker_order_id)) {
    MatchResult result{taker_order_id, incoming_quantity};
    result.mark_rejected(incoming_quantity);
    return result;
  }

  if (taker_kind == TakerKind::PostOnly &&
      incoming_quantity.value() > 0 &&
      has_matchable_depth()) {
    MatchResult result{taker_order_id, incoming_quantity};
    result.mark_rejected(incoming_quantity);
    return result;
  }

  if (taker_time_in_force.type == TimeInForce::FillOrKill &&
      incoming_quantity.value() > 0 &&
      matchable_quantity(incoming_quantity, taker_order_id) <
        incoming_quantity) {
    MatchResult result{taker_order_id, incoming_quantity};
    result.mark_killed(incoming_quantity);
    return result;
  }

  const std::size_t capacity = std::min(
    static_cast<std::size_t>(incoming_quantity.value()),
    order_count()
  );
  MatchResult result = MatchResult::with_capacity(
    taker_order_id,
    incoming_quantity,
    capacity
  );
  std::uint64_t remaining = incoming_quantity.value();
  std::unordered_set<InsertionSequence> set_aside;

  struct StepData {
    bool no_progress{};
    bool self_match{};
    OrderMatchResult match{};
    OrderId maker_id{};
    Side maker_side{Side::Buy};
    Price maker_price{};
    TimestampMs maker_timestamp{};
    std::uint64_t hidden_stranded{};
  };

  while (remaining > 0) {
    auto outcome = orders_.match_front(
      set_aside,
      [remaining, taker_order_id](
        InsertionSequence,
        const OrderType& maker
      ) {
        // Defensive guard for future synchronization changes. Under the
        // current level mutex, the initial lookup above catches this first.
        if (maker.get_id() == taker_order_id) {
          StepData step;
          step.self_match = true;
          return std::pair{FrontAction::set_aside(), std::move(step)};
        }

        OrderMatchResult match = maker.match_against(remaining);
        const bool no_progress =
          match.consumed == 0 &&
          match.hidden_reduced == 0 &&
          match.remaining_quantity == remaining &&
          match.updated_order.has_value();

        StepData step{
          no_progress,
          false,
          match,
          maker.get_id(),
          maker.get_side(),
          maker.get_price(),
          maker.get_timestamp(),
          !match.updated_order && match.hidden_reduced == 0
            ? maker.get_hidden_quantity().value()
            : 0
        };

        if (no_progress) {
          return std::pair{FrontAction::set_aside(), std::move(step)};
        }
        if (!match.updated_order) {
          return std::pair{FrontAction::remove(), std::move(step)};
        }

        auto replacement = std::make_shared<OrderType>(*match.updated_order);
        if (match.hidden_reduced > 0) {
          return std::pair{
            FrontAction::replace_at_tail(std::move(replacement)),
            std::move(step)
          };
        }
        return std::pair{
          FrontAction::keep_in_place(std::move(replacement)),
          std::move(step)
        };
      }
    );

    if (!outcome) {
      break;
    }

    StepData step = std::move(*outcome);
    if (step.self_match) {
      result.mark_rejected(incoming_quantity);
      return result;
    }
    if (step.no_progress) {
      continue;
    }

    const bool fully_consumed = !step.match.updated_order.has_value();
    if (step.match.consumed > 0) {
      subtract_checked(
        visible_quantity_,
        step.match.consumed,
        "visible_quantity"
      );

      Trade trade{
        trade_id_generator.next(),
        taker_order_id,
        step.maker_id,
        price_,
        Quantity{step.match.consumed},
        step.maker_side == Side::Buy ? Side::Sell : Side::Buy,
        timestamp
      };
      result.add_trade(std::move(trade));

      if (fully_consumed) {
        result.add_filled_order_id(step.maker_id);
      }

      try {
        stats_->record_execution(
          step.match.consumed,
          step.maker_price.value(),
          step.maker_timestamp,
          timestamp
        );
      } catch (const PriceLevelError&) {
        // Statistics are observational and must not invalidate a committed fill.
      }
    }

    remaining = step.match.remaining_quantity;

    if (fully_consumed) {
      const std::size_t current = order_count_.load(std::memory_order_relaxed);
      if (current == 0) {
        throw PriceLevelError("order count underflow during match");
      }
      order_count_.store(current - 1, std::memory_order_relaxed);
      if (current == 1) {
        resting_side_.reset();
      }
      if (step.hidden_stranded > 0) {
        subtract_checked(
          hidden_quantity_,
          step.hidden_stranded,
          "hidden_quantity"
        );
      }
    } else if (step.match.hidden_reduced > 0) {
      subtract_checked(
        hidden_quantity_,
        step.match.hidden_reduced,
        "hidden_quantity"
      );
      add_checked(
        visible_quantity_,
        step.match.hidden_reduced,
        "visible_quantity"
      );
    }
  }

  result.finalize(Quantity{remaining});
  return result;
}

MatchResult PriceLevel::match_order(
  const OrderType& order,
  TradeIdGenerator& trade_id_generator
) {
  return match_order(
    order.get_quantity(),
    order.get_id(),
    order.get_time_in_force(),
    taker_kind_for(order),
    order.get_timestamp(),
    trade_id_generator
  );
}

MatchResult PriceLevel::match_order(const OrderType& order) {
  static TradeIdGenerator trade_ids;
  return match_order(order, trade_ids);
}

std::optional<std::shared_ptr<OrderType>> PriceLevel::remove_order(
  OrderId order_id
) {
  auto removed = orders_.remove(order_id);
  if (!removed) {
    return std::nullopt;
  }

  subtract_checked(
    visible_quantity_,
    (*removed)->get_visible_quantity().value(),
    "visible_quantity"
  );
  subtract_checked(
    hidden_quantity_,
    (*removed)->get_hidden_quantity().value(),
    "hidden_quantity"
  );

  const std::size_t current = order_count_.load(std::memory_order_relaxed);
  if (current == 0) {
    throw PriceLevelError("order count underflow while removing order");
  }
  order_count_.store(current - 1, std::memory_order_relaxed);
  if (current == 1) {
    resting_side_.reset();
  }
  stats_->record_order_removed();
  return removed;
}

std::optional<std::shared_ptr<OrderType>> PriceLevel::update_quantity(
  OrderId order_id,
  Quantity new_quantity
) {
  const auto existing = orders_.find(order_id);
  if (!existing) {
    return std::nullopt;
  }

  const std::uint64_t previous_total = checked_total(**existing);
  OrderType updated_order = (*existing)->with_reduced_quantity(
    new_quantity.value()
  );
  const std::uint64_t new_total = checked_total(updated_order);
  auto replacement = std::make_shared<OrderType>(std::move(updated_order));

  std::optional<std::shared_ptr<OrderType>> previous;
  if (new_total > previous_total) {
    previous = orders_.replace_at_tail(order_id, replacement);
  } else {
    previous = orders_.update_in_place(order_id, replacement);
  }
  if (!previous) {
    return std::nullopt;
  }

  const std::uint64_t old_visible = (*previous)->get_visible_quantity().value();
  const std::uint64_t old_hidden = (*previous)->get_hidden_quantity().value();
  const std::uint64_t new_visible = replacement->get_visible_quantity().value();
  const std::uint64_t new_hidden = replacement->get_hidden_quantity().value();

  if (new_visible >= old_visible) {
    add_checked(visible_quantity_, new_visible - old_visible, "visible_quantity");
  } else {
    subtract_checked(visible_quantity_, old_visible - new_visible, "visible_quantity");
  }
  if (new_hidden >= old_hidden) {
    add_checked(hidden_quantity_, new_hidden - old_hidden, "hidden_quantity");
  } else {
    subtract_checked(hidden_quantity_, old_hidden - new_hidden, "hidden_quantity");
  }

  return replacement;
}

std::optional<std::shared_ptr<OrderType>> PriceLevel::update_order(
  const OrderUpdate& update
) {
  std::lock_guard<std::mutex> match_lock(match_mutex_);

  return std::visit(
    [this](const auto& operation)
      -> std::optional<std::shared_ptr<OrderType>> {
      using T = std::decay_t<decltype(operation)>;

      if constexpr (std::is_same_v<T, UpdatePrice>) {
        if (operation.new_price == price_) {
          throw PriceLevelError("cannot update an order to the same price");
        }
        return remove_order(operation.order_id);
      } else if constexpr (std::is_same_v<T, UpdateQuantity>) {
        return update_quantity(operation.order_id, operation.new_quantity);
      } else if constexpr (std::is_same_v<T, UpdatePriceAndQuantity>) {
        if (operation.new_price != price_) {
          return remove_order(operation.order_id);
        }
        return update_quantity(operation.order_id, operation.new_quantity);
      } else if constexpr (std::is_same_v<T, CancelOrder>) {
        return remove_order(operation.order_id);
      } else {
        if (operation.price != price_) {
          return remove_order(operation.order_id);
        }
        return update_quantity(operation.order_id, operation.quantity);
      }
    },
    update.variant()
  );
}

PriceLevelSnapshot PriceLevel::snapshot() const {
  std::lock_guard<std::mutex> match_lock(match_mutex_);
  // Persistence order is matching order. The timestamp-oriented
  // snapshot_orders() view is only for display and diagnostics.
  return PriceLevelSnapshot::with_orders_and_statistics(
    price_,
    snapshot_by_insertion_sequence(),
    stats_->snapshot()
  );
}

PriceLevelSnapshotPackage PriceLevel::snapshot_package() const {
  return PriceLevelSnapshotPackage{snapshot()};
}

std::string PriceLevel::snapshot_to_json() const {
  return snapshot_package().to_json_string();
}

PriceLevelData PriceLevel::data() const {
  const PriceLevelSnapshot current = snapshot();
  return PriceLevelData{
    current.price(),
    current.visible_quantity(),
    current.hidden_quantity(),
    current.order_count(),
    current.orders()
  };
}

void PriceLevel::add_checked(
  std::atomic<std::uint64_t>& target,
  std::uint64_t value,
  const char* field
) {
  std::uint64_t current = target.load(std::memory_order_relaxed);
  while (true) {
    if (value > std::numeric_limits<std::uint64_t>::max() - current) {
      throw PriceLevelError(std::string{field} + " overflow");
    }
    if (target.compare_exchange_weak(
          current,
          current + value,
          std::memory_order_relaxed,
          std::memory_order_relaxed
        )) {
      return;
    }
  }
}

void PriceLevel::subtract_checked(
  std::atomic<std::uint64_t>& target,
  std::uint64_t value,
  const char* field
) {
  std::uint64_t current = target.load(std::memory_order_relaxed);
  while (true) {
    if (value > current) {
      throw PriceLevelError(std::string{field} + " underflow");
    }
    if (target.compare_exchange_weak(
          current,
          current - value,
          std::memory_order_relaxed,
          std::memory_order_relaxed
        )) {
      return;
    }
  }
}

std::ostream& operator<<(std::ostream& output, const PriceLevel& level) {
  output << "PriceLevel:price=" << level.price()
         << ";visible_quantity=" << level.visible_quantity()
         << ";hidden_quantity=" << level.hidden_quantity()
         << ";order_count=" << level.order_count();
  return output;
}

} // namespace lemon
