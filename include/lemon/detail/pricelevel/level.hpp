#pragma once

#include <atomic>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <lemon/detail/errors.hpp>
#include <lemon/detail/execution/match_result.hpp>
#include <lemon/detail/orders/order_type.hpp>
#include <lemon/detail/orders/order_update.hpp>
#include <lemon/detail/pricelevel/order_queue.hpp>
#include <lemon/detail/pricelevel/snapshot.hpp>
#include <lemon/detail/pricelevel/statistics.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

class PriceLevel {
public:
  explicit PriceLevel(Price price);

  static PriceLevel from_snapshot(PriceLevelSnapshot snapshot);
  static PriceLevel from_snapshot_package(PriceLevelSnapshotPackage package);
  static PriceLevel from_snapshot_json(std::string_view data);
  static PriceLevel from_data(const PriceLevelData& data);

  Price price() const;
  Quantity visible_quantity() const;
  Quantity hidden_quantity() const;
  Quantity total_quantity() const;
  std::size_t order_count() const;
  std::shared_ptr<PriceLevelStatistics> stats() const;

  std::shared_ptr<OrderType> add_order(OrderType order);

  // Materialized views. iter_orders() follows FIFO insertion order; the name
  // is retained for source compatibility with the existing API.
  std::vector<std::shared_ptr<OrderType>> iter_orders() const;
  std::vector<std::shared_ptr<OrderType>> snapshot_orders() const;
  std::vector<std::shared_ptr<OrderType>> snapshot_by_insertion_sequence() const;
  void snapshot_by_sequence_into(
    std::vector<std::shared_ptr<OrderType>>& result
  ) const;

  Quantity matchable_quantity(Quantity incoming_quantity) const;

  MatchResult match_order(
    Quantity incoming_quantity,
    OrderId taker_order_id,
    TimeInForcePolicy taker_time_in_force,
    TakerKind taker_kind,
    TimestampMs timestamp,
    TradeIdGenerator& trade_id_generator
  );

  MatchResult match_order(
    const OrderType& order,
    TradeIdGenerator& trade_id_generator
  );

  // Convenience overload using a process-wide sequential trade ID source.
  MatchResult match_order(const OrderType& order);

  std::optional<std::shared_ptr<OrderType>> update_order(
    const OrderUpdate& update
  );

  PriceLevelSnapshot snapshot() const;
  PriceLevelSnapshotPackage snapshot_package() const;
  std::string snapshot_to_json() const;
  PriceLevelData data() const;

  friend bool operator==(const PriceLevel& left, const PriceLevel& right) {
    return left.price() == right.price();
  }

  friend auto operator<=>(const PriceLevel& left, const PriceLevel& right) {
    return left.price() <=> right.price();
  }

  friend std::ostream& operator<<(std::ostream& output, const PriceLevel& level);

private:
  PriceLevel(
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    std::size_t order_count,
    std::vector<std::shared_ptr<OrderType>> orders,
    PriceLevelStatisticsData statistics
  );

  bool has_matchable_depth() const;
  std::optional<std::shared_ptr<OrderType>> remove_order(OrderId order_id);
  std::optional<std::shared_ptr<OrderType>> update_quantity(
    OrderId order_id,
    Quantity new_quantity
  );

  static void add_checked(
    std::atomic<std::uint64_t>& target,
    std::uint64_t value,
    const char* field
  );

  static void subtract_checked(
    std::atomic<std::uint64_t>& target,
    std::uint64_t value,
    const char* field
  );

  Price price_;
  std::atomic<std::uint64_t> visible_quantity_{0};
  std::atomic<std::uint64_t> hidden_quantity_{0};
  std::atomic_size_t order_count_{0};
  OrderQueue orders_;
  std::shared_ptr<PriceLevelStatistics> stats_;
  mutable std::mutex match_mutex_;
};

} // namespace lemon
