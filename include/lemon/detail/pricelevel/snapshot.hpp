#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/json.hpp>
#include <openssl/sha.h>

#include <lemon/detail/errors.hpp>
#include <lemon/detail/orders/order_type.hpp>
#include <lemon/detail/pricelevel/statistics.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

inline constexpr std::uint32_t snapshot_format_version = 2;

class PriceLevelSnapshot {
public:
  PriceLevelSnapshot()
    : statistics_(PriceLevelStatistics{}.snapshot()) {
  }

  explicit PriceLevelSnapshot(Price price)
    : price_(price),
      statistics_(PriceLevelStatistics{}.snapshot()) {
  }

  PriceLevelSnapshot(
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    std::size_t order_count,
    std::vector<std::shared_ptr<OrderType>> orders,
    PriceLevelStatisticsData statistics = PriceLevelStatistics{}.snapshot()
  )
    : price_(price),
      visible_quantity_(visible_quantity),
      hidden_quantity_(hidden_quantity),
      order_count_(order_count),
      orders_(std::move(orders)),
      statistics_(statistics) {
  }

  PriceLevelSnapshot(
    Price price,
    const std::vector<std::shared_ptr<OrderType>>& orders
  )
    : PriceLevelSnapshot(with_orders(price, orders)) {
  }

  static PriceLevelSnapshot with_orders(
    Price price,
    std::vector<std::shared_ptr<OrderType>> orders
  ) {
    return with_orders_and_statistics(
      price,
      std::move(orders),
      PriceLevelStatistics{}.snapshot()
    );
  }

  static PriceLevelSnapshot with_orders_and_statistics(
    Price price,
    std::vector<std::shared_ptr<OrderType>> orders,
    PriceLevelStatisticsData statistics
  ) {
    PriceLevelSnapshot snapshot{
      price,
      Quantity::zero(),
      Quantity::zero(),
      0,
      std::move(orders),
      statistics
    };
    snapshot.refresh_aggregates();
    return snapshot;
  }

  static PriceLevelSnapshot from_raw_parts(
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    std::size_t order_count,
    std::vector<std::shared_ptr<OrderType>> orders,
    PriceLevelStatisticsData statistics = PriceLevelStatistics{}.snapshot()
  ) {
    return PriceLevelSnapshot{
      price,
      visible_quantity,
      hidden_quantity,
      order_count,
      std::move(orders),
      statistics
    };
  }

  Price price() const {
    return price_;
  }

  Quantity visible_quantity() const {
    return visible_quantity_;
  }

  Quantity hidden_quantity() const {
    return hidden_quantity_;
  }

  std::size_t order_count() const {
    return order_count_;
  }

  const PriceLevelStatisticsData& statistics() const {
    return statistics_;
  }

  const std::vector<std::shared_ptr<OrderType>>& orders() const {
    return orders_;
  }

  std::vector<std::shared_ptr<OrderType>> into_orders() && {
    return std::move(orders_);
  }

  // Compatibility overload for existing const/value call sites.
  std::vector<std::shared_ptr<OrderType>> into_orders() const & {
    return orders_;
  }

  Quantity total_quantity() const {
    if (hidden_quantity_.value() >
        std::numeric_limits<value_type>::max() - visible_quantity_.value()) {
      throw PriceLevelError("snapshot total quantity overflow");
    }
    return Quantity{visible_quantity_.value() + hidden_quantity_.value()};
  }

  void refresh_aggregates() {
    std::size_t count = 0;
    std::uint64_t visible_total = 0;
    std::uint64_t hidden_total = 0;

    for (const auto& order : orders_) {
      if (!order) {
        continue;
      }
      if (order->get_visible_quantity().value() >
          std::numeric_limits<std::uint64_t>::max() - visible_total) {
        throw PriceLevelError("snapshot visible quantity overflow");
      }
      if (order->get_hidden_quantity().value() >
          std::numeric_limits<std::uint64_t>::max() - hidden_total) {
        throw PriceLevelError("snapshot hidden quantity overflow");
      }
      ++count;
      visible_total += order->get_visible_quantity().value();
      hidden_total += order->get_hidden_quantity().value();
    }

    order_count_ = count;
    visible_quantity_ = Quantity{visible_total};
    hidden_quantity_ = Quantity{hidden_total};
  }

private:
  Price price_{};
  Quantity visible_quantity_{};
  Quantity hidden_quantity_{};
  std::size_t order_count_{};
  std::vector<std::shared_ptr<OrderType>> orders_{};
  PriceLevelStatisticsData statistics_{};
};

class PriceLevelSnapshotPackage {
public:
  PriceLevelSnapshotPackage() = default;

  explicit PriceLevelSnapshotPackage(PriceLevelSnapshot snapshot)
    : version_(snapshot_format_version),
      snapshot_(std::move(snapshot)) {
    snapshot_.refresh_aggregates();
    checksum_ = compute_checksum(snapshot_);
  }

  std::uint32_t version() const {
    return version_;
  }

  const PriceLevelSnapshot& get_snapshot() const {
    return snapshot_;
  }

  const std::string& get_checksum() const {
    return checksum_;
  }

  boost::json::object to_json() const {
    return boost::json::object{
      {"version", version_},
      {"snapshot", snapshot_to_json(snapshot_)},
      {"checksum", checksum_}
    };
  }

  std::string to_json_string() const {
    return boost::json::serialize(to_json());
  }

  static PriceLevelSnapshotPackage from_json_string(std::string_view data) {
    try {
      const boost::json::value parsed = boost::json::parse(data);
      const auto& package = require_object(parsed, "snapshot package");

      const std::uint64_t version = require_uint(package, "version");
      if (version > std::numeric_limits<std::uint32_t>::max()) {
        throw PriceLevelError("snapshot version is out of range");
      }

      PriceLevelSnapshotPackage result;
      result.version_ = static_cast<std::uint32_t>(version);
      result.snapshot_ = snapshot_from_json(
        require_object(require(package, "snapshot"), "snapshot")
      );
      result.checksum_ = require_string(package, "checksum");
      return result;
    } catch (const PriceLevelError&) {
      throw;
    } catch (const std::exception& error) {
      throw PriceLevelError(
        std::string{"snapshot deserialization failed: "} + error.what()
      );
    }
  }

  void validate() const {
    if (version_ != snapshot_format_version) {
      throw PriceLevelError(
        "unsupported snapshot version " + std::to_string(version_) +
        "; expected " + std::to_string(snapshot_format_version)
      );
    }

    const std::string actual = compute_checksum(snapshot_);
    if (actual != checksum_) {
      throw PriceLevelError(
        "snapshot checksum mismatch: expected " + checksum_ +
        ", actual " + actual
      );
    }
  }

  PriceLevelSnapshot into_snapshot() && {
    validate();
    return std::move(snapshot_);
  }

  static std::string compute_checksum(const PriceLevelSnapshot& snapshot) {
    const std::string payload = boost::json::serialize(snapshot_to_json(snapshot));
    return sha256(payload);
  }

private:
  static const boost::json::value& require(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto* value = object.if_contains(key);
    if (!value) {
      throw PriceLevelError("missing JSON field: " + std::string{key});
    }
    return *value;
  }

  static const boost::json::object& require_object(
    const boost::json::value& value,
    std::string_view field
  ) {
    if (!value.is_object()) {
      throw PriceLevelError(std::string{field} + " must be a JSON object");
    }
    return value.as_object();
  }

  static std::uint64_t require_uint(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto& value = require(object, key);
    if (value.is_uint64()) {
      return value.as_uint64();
    }
    if (value.is_int64() && value.as_int64() >= 0) {
      return static_cast<std::uint64_t>(value.as_int64());
    }
    throw PriceLevelError("JSON field must be an unsigned integer: " + std::string{key});
  }

  static std::int64_t require_int(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto& value = require(object, key);
    if (value.is_int64()) {
      return value.as_int64();
    }
    if (value.is_uint64() &&
        value.as_uint64() <= static_cast<std::uint64_t>(
          std::numeric_limits<std::int64_t>::max()
        )) {
      return static_cast<std::int64_t>(value.as_uint64());
    }
    throw PriceLevelError("JSON field must be an integer: " + std::string{key});
  }

  static bool require_bool(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto& value = require(object, key);
    if (!value.is_bool()) {
      throw PriceLevelError("JSON field must be a boolean: " + std::string{key});
    }
    return value.as_bool();
  }

  static std::string require_string(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto& value = require(object, key);
    if (!value.is_string()) {
      throw PriceLevelError("JSON field must be a string: " + std::string{key});
    }
    return std::string{value.as_string()};
  }

  static std::optional<std::uint64_t> optional_uint(
    const boost::json::object& object,
    std::string_view key
  ) {
    const auto* value = object.if_contains(key);
    if (!value || value->is_null()) {
      return std::nullopt;
    }
    if (value->is_uint64()) {
      return value->as_uint64();
    }
    if (value->is_int64() && value->as_int64() >= 0) {
      return static_cast<std::uint64_t>(value->as_int64());
    }
    throw PriceLevelError("JSON field must be an unsigned integer: " + std::string{key});
  }

  static std::string user_hash_to_hex(const UserHash& user_hash) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const std::uint8_t byte : user_hash) {
      stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
  }

  static UserHash user_hash_from_hex(std::string_view text) {
    if (text.size() != 64) {
      throw PriceLevelError("user_id must contain 64 hexadecimal characters");
    }

    UserHash result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
      unsigned int byte = 0;
      const char* begin = text.data() + index * 2;
      const char* end = begin + 2;
      const auto conversion = std::from_chars(begin, end, byte, 16);
      if (conversion.ec != std::errc{} || conversion.ptr != end) {
        throw PriceLevelError("user_id contains invalid hexadecimal data");
      }
      result[index] = static_cast<std::uint8_t>(byte);
    }
    return result;
  }

  static TimeInForcePolicy time_in_force_from_json(
    const boost::json::object& object
  ) {
    const std::string text = require_string(object, "time_in_force");
    const auto expires = optional_uint(object, "expires_at");
    if (text == "GTC") {
      return {TimeInForce::GoodTillCanceled, std::nullopt};
    }
    if (text == "IOC") {
      return {TimeInForce::ImmediateOrCancel, std::nullopt};
    }
    if (text == "FOK") {
      return {TimeInForce::FillOrKill, std::nullopt};
    }
    if (text == "GTD") {
      return {TimeInForce::GoodTillDate, expires};
    }
    if (text == "DAY") {
      return {TimeInForce::Day, std::nullopt};
    }
    throw PriceLevelError("unknown time_in_force: " + text);
  }

  static Side side_from_json(const boost::json::object& object) {
    const std::string side = require_string(object, "side");
    if (side == "buy") {
      return Side::Buy;
    }
    if (side == "sell") {
      return Side::Sell;
    }
    throw PriceLevelError("unknown order side: " + side);
  }

  static PegReferenceType peg_reference_from_json(
    const boost::json::object& object
  ) {
    const std::string reference = require_string(object, "reference_price_type");
    if (reference == "BestBid") return PegReferenceType::BestBid;
    if (reference == "BestAsk") return PegReferenceType::BestAsk;
    if (reference == "MidPrice") return PegReferenceType::MidPrice;
    if (reference == "LastTrade") return PegReferenceType::LastTrade;
    throw PriceLevelError("unknown peg reference: " + reference);
  }

  static boost::json::object order_to_json(const OrderType& order) {
    boost::json::object json{
      {"id", order.get_id()},
      {"kind", std::string{to_string(order.kind())}},
      {"side", order.is_buy() ? "buy" : "sell"},
      {"price", order.get_price().value()},
      {"quantity", order.get_quantity().value()},
      {"visible_quantity", order.get_visible_quantity().value()},
      {"hidden_quantity", order.get_hidden_quantity().value()},
      {"timestamp", order.get_timestamp()},
      {"time_in_force", std::string{order.get_time_in_force().to_string()}},
      {"user_id", user_hash_to_hex(order.get_user_id())}
    };

    if (const auto expires_at = order.get_expires_at()) {
      json["expires_at"] = *expires_at;
    } else if (const auto policy_expiry =
                 order.get_time_in_force().expiry_timestamp) {
      json["expires_at"] = *policy_expiry;
    }

    std::visit(
      [&json](const auto& variant) {
        using T = std::decay_t<decltype(variant)>;
        if constexpr (std::is_same_v<T, TrailingStopOrder>) {
          json["trail_amount"] = variant.trail_amount.value();
          json["last_reference_price"] = variant.last_reference_price.value();
        } else if constexpr (std::is_same_v<T, PeggedOrder>) {
          json["reference_price_offset"] = variant.reference_price_offset;
          json["reference_price_type"] =
            std::string{to_string(variant.reference_price_type)};
        } else if constexpr (std::is_same_v<T, ReserveOrder>) {
          json["replenish_threshold"] = variant.replenish_threshold.value();
          if (variant.replenish_amount) {
            json["replenish_amount"] = variant.replenish_amount->value();
          } else {
            json["replenish_amount"] = nullptr;
          }
          json["auto_replenish"] = variant.auto_replenish;
        }
      },
      order.variant()
    );

    return json;
  }

  static std::shared_ptr<OrderType> order_from_json(
    const boost::json::object& object
  ) {
    const OrderId id = require_uint(object, "id");
    const Side side = side_from_json(object);
    const Price price{require_uint(object, "price")};
    const TimestampMs timestamp = require_uint(object, "timestamp");
    const TimeInForcePolicy time_in_force = time_in_force_from_json(object);
    const auto expires = optional_uint(object, "expires_at");
    const UserHash user_id = user_hash_from_hex(require_string(object, "user_id"));
    const std::string kind = require_string(object, "kind");

    OrderType order;
    if (kind == "standard") {
      order = OrderType::standard(
        id, side, price, Quantity{require_uint(object, "quantity")},
        timestamp, time_in_force, expires, user_id
      );
    } else if (kind == "iceberg") {
      order = OrderType::iceberg(
        id, side, price,
        Quantity{require_uint(object, "visible_quantity")},
        Quantity{require_uint(object, "hidden_quantity")},
        timestamp, time_in_force, expires, user_id
      );
    } else if (kind == "post_only") {
      order = OrderType::post_only(
        id, side, price, Quantity{require_uint(object, "quantity")},
        timestamp, time_in_force, expires, user_id
      );
    } else if (kind == "trailing_stop") {
      order = OrderType::trailing_stop(
        id, side, price, Quantity{require_uint(object, "quantity")},
        Quantity{require_uint(object, "trail_amount")},
        Price{require_uint(object, "last_reference_price")},
        timestamp, time_in_force, expires, user_id
      );
    } else if (kind == "pegged") {
      order = OrderType::pegged(
        id, side, price, Quantity{require_uint(object, "quantity")},
        require_int(object, "reference_price_offset"),
        peg_reference_from_json(object), timestamp,
        time_in_force, expires, user_id
      );
    } else if (kind == "market_to_limit") {
      order = OrderType::market_to_limit(
        id, side, price, Quantity{require_uint(object, "quantity")},
        timestamp, time_in_force, expires, user_id
      );
    } else if (kind == "reserve") {
      std::optional<NonZeroQuantity> replenish;
      if (const auto amount = optional_uint(object, "replenish_amount")) {
        replenish = NonZeroQuantity{*amount};
      }
      order = OrderType::reserve(
        id, side, price,
        Quantity{require_uint(object, "visible_quantity")},
        Quantity{require_uint(object, "hidden_quantity")},
        Quantity{require_uint(object, "replenish_threshold")},
        replenish,
        require_bool(object, "auto_replenish"),
        timestamp, time_in_force, expires, user_id
      );
    } else {
      throw PriceLevelError("unknown order kind: " + kind);
    }

    return std::make_shared<OrderType>(std::move(order));
  }

  static boost::json::object statistics_to_json(
    const PriceLevelStatisticsData& statistics
  ) {
    return boost::json::object{
      {"orders_added", statistics.orders_added},
      {"orders_removed", statistics.orders_removed},
      {"orders_executed", statistics.orders_executed},
      {"quantity_executed", statistics.quantity_executed},
      {"value_executed", statistics.value_executed},
      {"last_execution_timestamp", statistics.last_execution_timestamp},
      {"first_arrival_timestamp", statistics.first_arrival_timestamp},
      {"sum_waiting_time", statistics.sum_waiting_time}
    };
  }

  static PriceLevelStatisticsData statistics_from_json(
    const boost::json::object& object
  ) {
    return PriceLevelStatisticsData{
      require_uint(object, "orders_added"),
      require_uint(object, "orders_removed"),
      require_uint(object, "orders_executed"),
      require_uint(object, "quantity_executed"),
      require_uint(object, "value_executed"),
      require_uint(object, "last_execution_timestamp"),
      require_uint(object, "first_arrival_timestamp"),
      require_uint(object, "sum_waiting_time")
    };
  }

  static boost::json::object snapshot_to_json(
    const PriceLevelSnapshot& snapshot
  ) {
    boost::json::array orders;
    orders.reserve(snapshot.orders().size());
    for (const auto& order : snapshot.orders()) {
      if (order) {
        orders.push_back(order_to_json(*order));
      }
    }

    return boost::json::object{
      {"price", snapshot.price().value()},
      {"visible_quantity", snapshot.visible_quantity().value()},
      {"hidden_quantity", snapshot.hidden_quantity().value()},
      {"order_count", snapshot.order_count()},
      {"orders", std::move(orders)},
      {"statistics", statistics_to_json(snapshot.statistics())}
    };
  }

  static PriceLevelSnapshot snapshot_from_json(
    const boost::json::object& object
  ) {
    const auto& order_values = require(object, "orders");
    if (!order_values.is_array()) {
      throw PriceLevelError("orders must be a JSON array");
    }

    std::vector<std::shared_ptr<OrderType>> orders;
    orders.reserve(order_values.as_array().size());
    for (const auto& value : order_values.as_array()) {
      orders.push_back(order_from_json(require_object(value, "order")));
    }

    return PriceLevelSnapshot::from_raw_parts(
      Price{require_uint(object, "price")},
      Quantity{require_uint(object, "visible_quantity")},
      Quantity{require_uint(object, "hidden_quantity")},
      static_cast<std::size_t>(require_uint(object, "order_count")),
      std::move(orders),
      statistics_from_json(
        require_object(require(object, "statistics"), "statistics")
      )
    );
  }

  static std::string sha256(const std::string& payload) {
    unsigned char digest[SHA256_DIGEST_LENGTH]{};
    SHA256(
      reinterpret_cast<const unsigned char*>(payload.data()),
      payload.size(),
      digest
    );

    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const unsigned char byte : digest) {
      stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
  }

  std::uint32_t version_{snapshot_format_version};
  PriceLevelSnapshot snapshot_{};
  std::string checksum_{};
};

struct PriceLevelData {
  Price price{};
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  std::size_t order_count{};
  std::vector<std::shared_ptr<OrderType>> orders{};
};

} // namespace lemon
