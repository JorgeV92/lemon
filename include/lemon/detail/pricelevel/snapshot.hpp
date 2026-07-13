#pragma once

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <boost/json.hpp>
#include <openssl/sha.h>

#include <lemon/detail/orders/order_type.hpp>
#include <lemon/utils/value.hpp>

namespace lemon {

class PriceLevelSnapshot {
public:
  PriceLevelSnapshot() = default;

  explicit PriceLevelSnapshot(Price price)
    : price_(price) {
  }

  PriceLevelSnapshot(Price price,
                     Quantity visible_quantity,
                     Quantity hidden_quantity,
                     std::size_t order_count,
                     std::vector<std::shared_ptr<OrderType>> orders)
    : price_(price),
      visible_quantity_(visible_quantity),
      hidden_quantity_(hidden_quantity),
      order_count_(order_count),
      orders_(std::move(orders)) {
  }

  PriceLevelSnapshot(Price price, const std::vector<std::shared_ptr<OrderType>>& orders)
    : PriceLevelSnapshot(
        price,
        Quantity::zero(),
        Quantity::zero(),
        orders.size(),
        orders
      ) {
  }

  static PriceLevelSnapshot with_orders(
    Price price,
    std::vector<std::shared_ptr<OrderType>> orders
  ) {
    return PriceLevelSnapshot{
      price,
      Quantity::zero(),
      Quantity::zero(),
      orders.size(),
      std::move(orders)
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

  const std::vector<std::shared_ptr<OrderType>>& orders() const {
    return orders_;
  }

  std::vector<std::shared_ptr<OrderType>> into_orders() const {
    return orders_;
  }

  static PriceLevelSnapshot from_raw_parts(
    Price price,
    Quantity visible_quantity,
    Quantity hidden_quantity,
    std::size_t order_count,
    std::vector<std::shared_ptr<OrderType>> orders
  ) {
    return PriceLevelSnapshot{
      price,
      visible_quantity,
      hidden_quantity,
      order_count,
      std::move(orders)
    };
  }

  Quantity total_quantity() const {
    return Quantity{visible_quantity_.value() + hidden_quantity_.value()};
  }

  void refresh_aggregates() {
    std::size_t order_count = 0;
    std::uint64_t visible_total = 0;
    std::uint64_t hidden_total = 0;

    for (const auto& order : orders_) {
      if (!order) {
        continue;
      }

      ++order_count;
      visible_total += order->get_visible_quantity().value();
      hidden_total += order->get_hidden_quantity().value();
    }

    order_count_ = order_count;
    visible_quantity_ = Quantity{visible_total};
    hidden_quantity_ = Quantity{hidden_total};
  }

private:
  Price price_{};
  Quantity visible_quantity_{};
  Quantity hidden_quantity_{};
  std::size_t order_count_{};
  std::vector<std::shared_ptr<OrderType>> orders_{};
};

class PriceLevelSnapshotPackage {
public:
  PriceLevelSnapshotPackage() = default;

  explicit PriceLevelSnapshotPackage(PriceLevelSnapshot snapshot)
    : snapshot_(std::move(snapshot)) {
    snapshot_.refresh_aggregates();
    checksum_ = compute_checksum(snapshot_);
  }

  const PriceLevelSnapshot& get_snapshot() const {
    return snapshot_;
  }

  const std::string& get_checksum() const {
    return checksum_;
  }

  boost::json::object to_json() const {
    return boost::json::object{
      {"snapshot", snapshot_to_json(snapshot_)},
      {"checksum", checksum_}
    };
  }

  std::string to_json_string() const {
    return boost::json::serialize(to_json());
  }

  // snapshots can be saved, sent over a network, replayed, or 
  // validated later. the checksum tells you wether the snapshot 
  // contents are exactly the same. 
  static std::string compute_checksum(const PriceLevelSnapshot& snapshot) {
    const boost::json::object json = snapshot_to_json(snapshot);
    const std::string payload = boost::json::serialize(json);
    return sha256(payload);
  }

private:
  static std::string user_hash_to_hex(const UserHash& user_hash) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (const std::uint8_t byte : user_hash) {
      stream << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return stream.str();
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
    }

    std::visit(
      [&json](const auto& variant) {
        using T = std::decay_t<decltype(variant)>;
        if constexpr (std::is_same_v<T, TrailingStopOrder>) {
          json["trail_amount"] = variant.trail_amount.value();
          json["last_reference_price"] =
            variant.last_reference_price.value();
        } else if constexpr (std::is_same_v<T, PeggedOrder>) {
          json["reference_price_offset"] = variant.reference_price_offset;
          json["reference_price_type"] =
            std::string{to_string(variant.reference_price_type)};
        } else if constexpr (std::is_same_v<T, ReserveOrder>) {
          json["replenish_threshold"] =
            variant.replenish_threshold.value();
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

  static boost::json::object snapshot_to_json(const PriceLevelSnapshot& snapshot) {
    boost::json::array orders;
    orders.reserve(snapshot.orders().size());

    for (const auto& order : snapshot.orders()) {
      if (!order) {
        continue;
      }

      orders.push_back(order_to_json(*order));
    }

    return boost::json::object{
      {"price", snapshot.price().value()},
      {"visible_quantity", snapshot.visible_quantity().value()},
      {"hidden_quantity", snapshot.hidden_quantity().value()},
      {"order_count", snapshot.order_count()},
      {"orders", std::move(orders)}
    };
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

  PriceLevelSnapshot snapshot_{};
  std::string checksum_{};
};

struct PriceLevelData {
  Price price{};
  Quantity visible_quantity{};
  Quantity hidden_quantity{};
  std::size_t order_count{};
};

} // namespace lemon
