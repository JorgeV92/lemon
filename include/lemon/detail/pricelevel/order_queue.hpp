#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <lemon/detail/orders/order_type.hpp>

namespace lemon {

using InsertionSequence = std::uint64_t;

// Describes the mutation that OrderQueue::match_front commits while the
// queue mutex is still held. Keeping the decision and the mutation under the
// same lock prevents a cancel from slipping between a match and replacement.
class FrontAction {
public:
  enum class Kind : std::uint8_t {
    Remove,
    KeepInPlace,
    ReplaceAtTail,
    SetAside,
    Abort
  };

  static FrontAction remove() {
    return FrontAction{Kind::Remove, nullptr};
  }

  static FrontAction keep_in_place(std::shared_ptr<OrderType> replacement) {
    return FrontAction{Kind::KeepInPlace, std::move(replacement)};
  }

  static FrontAction replace_at_tail(std::shared_ptr<OrderType> replacement) {
    return FrontAction{Kind::ReplaceAtTail, std::move(replacement)};
  }

  static FrontAction set_aside() {
    return FrontAction{Kind::SetAside, nullptr};
  }

  static FrontAction abort() {
    return FrontAction{Kind::Abort, nullptr};
  }

  Kind kind() const {
    return kind_;
  }

  const std::shared_ptr<OrderType>& replacement() const {
    return replacement_;
  }

private:
  FrontAction(Kind kind, std::shared_ptr<OrderType> replacement)
    : kind_(kind), replacement_(std::move(replacement)) {
  }

  Kind kind_;
  std::shared_ptr<OrderType> replacement_;
};

class OrderQueue {
public:
  OrderQueue() = default;

  explicit OrderQueue(const std::vector<std::shared_ptr<OrderType>>& orders) {
    for (const auto& order : orders) {
      if (order) {
        push(order);
      }
    }
  }

  // Adds an order at the tail (newest priority). Pushing an existing ID
  // replaces its previous entry and gives it a new tail sequence.
  void push(std::shared_ptr<OrderType> order) {
    if (!order) {
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const OrderId order_id = order->get_id();
    const InsertionSequence sequence = next_sequence_unlocked();

    if (const auto existing = orders_.find(order_id); existing != orders_.end()) {
      index_.erase(existing->second.sequence);
    }

    orders_.insert_or_assign(
      order_id,
      QueueEntry{sequence, std::move(order)}
    );
    index_.insert_or_assign(sequence, order_id);
  }

  // Pops and removes the oldest order together with its insertion sequence.
  std::optional<std::pair<InsertionSequence, std::shared_ptr<OrderType>>> pop_entry() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pop_entry_unlocked();
  }

  // Pops and removes the oldest order.
  std::optional<std::shared_ptr<OrderType>> pop() {
    auto entry = pop_entry();
    if (!entry) {
      return std::nullopt;
    }
    return std::move(entry->second);
  }

  std::optional<std::shared_ptr<OrderType>> find(OrderId order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
      return std::nullopt;
    }

    return it->second.order;
  }

  // Removes an order by ID and cleans both lookup and priority indexes.
  std::optional<std::shared_ptr<OrderType>> remove(OrderId order_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
      return std::nullopt;
    }

    auto order = it->second.order;
    index_.erase(it->second.sequence);
    orders_.erase(it);
    return order;
  }

  // Replaces an order without changing its insertion sequence, preserving
  // price-time priority. Returns the previous order when present.
  std::optional<std::shared_ptr<OrderType>> update_in_place(
    OrderId order_id,
    std::shared_ptr<OrderType> replacement
  ) {
    validate_replacement(order_id, replacement);
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
      return std::nullopt;
    }

    auto previous = std::move(it->second.order);
    it->second.order = std::move(replacement);
    return previous;
  }

  // Replaces an order and assigns it a fresh tail sequence, causing it to
  // lose priority. Returns the previous order when present.
  std::optional<std::shared_ptr<OrderType>> replace_at_tail(
    OrderId order_id,
    std::shared_ptr<OrderType> replacement
  ) {
    validate_replacement(order_id, replacement);
    std::lock_guard<std::mutex> lock(mutex_);

    const auto it = orders_.find(order_id);
    if (it == orders_.end()) {
      return std::nullopt;
    }

    const InsertionSequence old_sequence = it->second.sequence;
    const InsertionSequence new_sequence = next_sequence_unlocked();
    auto previous = std::move(it->second.order);

    index_.insert_or_assign(new_sequence, order_id);
    it->second.sequence = new_sequence;
    it->second.order = std::move(replacement);
    index_.erase(old_sequence);
    return previous;
  }

  // Selects the oldest order not present in set_aside, calls decide while the
  // queue lock is held, and commits the returned action before unlocking.
  //
  // decide must return std::pair<FrontAction, Result> and must not call back
  // into this OrderQueue or block, because the queue mutex is held. The Result
  // is returned to the caller; std::nullopt means no eligible front existed.
  template <typename Decide>
  auto match_front(
    std::unordered_set<InsertionSequence>& set_aside,
    Decide&& decide
  ) {
    using Decision = std::invoke_result_t<
      Decide&,
      InsertionSequence,
      const OrderType&
    >;
    using Result = typename std::decay_t<Decision>::second_type;

    std::lock_guard<std::mutex> lock(mutex_);

    while (true) {
      auto index_it = index_.begin();
      while (index_it != index_.end() && set_aside.contains(index_it->first)) {
        ++index_it;
      }

      if (index_it == index_.end()) {
        return std::optional<Result>{};
      }

      const InsertionSequence sequence = index_it->first;
      const OrderId order_id = index_it->second;
      const auto order_it = orders_.find(order_id);

      // Heal any stale priority entry defensively. All public mutation paths
      // maintain both indexes under the same mutex, so this should not occur.
      if (order_it == orders_.end() ||
          order_it->second.sequence != sequence ||
          !order_it->second.order) {
        index_.erase(index_it);
        continue;
      }

      auto decision = std::invoke(
        decide,
        sequence,
        std::as_const(*order_it->second.order)
      );
      FrontAction action = std::move(decision.first);
      Result result = std::move(decision.second);

      if (action.kind() == FrontAction::Kind::KeepInPlace ||
          action.kind() == FrontAction::Kind::ReplaceAtTail) {
        validate_replacement(order_id, action.replacement());
      }

      switch (action.kind()) {
        case FrontAction::Kind::Remove:
          orders_.erase(order_it);
          index_.erase(index_it);
          break;

        case FrontAction::Kind::KeepInPlace:
          order_it->second.order = action.replacement();
          break;

        case FrontAction::Kind::ReplaceAtTail: {
          const InsertionSequence new_sequence = next_sequence_unlocked();
          index_.insert_or_assign(new_sequence, order_id);
          order_it->second.sequence = new_sequence;
          order_it->second.order = action.replacement();
          index_.erase(index_it);
          break;
        }

        case FrontAction::Kind::SetAside:
          set_aside.insert(sequence);
          break;

        case FrontAction::Kind::Abort:
          // Deliberately leave the FIFO-front maker and both indexes intact.
          break;
      }

      return std::optional<Result>{std::move(result)};
    }
  }

  // Returns a deterministic snapshot sorted by (timestamp, insertion sequence).
  std::vector<std::shared_ptr<OrderType>> snapshot_vec() const {
    std::lock_guard<std::mutex> lock(mutex_);

    struct SnapshotEntry {
      InsertionSequence sequence;
      std::shared_ptr<OrderType> order;
    };

    std::vector<SnapshotEntry> entries;
    entries.reserve(orders_.size());

    for (const auto& [id, entry] : orders_) {
      static_cast<void>(id);
      if (entry.order) {
        entries.push_back(SnapshotEntry{entry.sequence, entry.order});
      }
    }

    std::sort(
      entries.begin(),
      entries.end(),
      [](const SnapshotEntry& a, const SnapshotEntry& b) {
        const auto a_timestamp = a.order->get_timestamp();
        const auto b_timestamp = b.order->get_timestamp();
        if (a_timestamp != b_timestamp) {
          return a_timestamp < b_timestamp;
        }
        return a.sequence < b.sequence;
      }
    );

    std::vector<std::shared_ptr<OrderType>> result;
    result.reserve(entries.size());
    for (auto& entry : entries) {
      result.push_back(std::move(entry.order));
    }
    return result;
  }

  // Returns the exact FIFO order used by pop() and match_front().
  std::vector<std::shared_ptr<OrderType>> snapshot_by_sequence() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::shared_ptr<OrderType>> result;
    result.reserve(orders_.size());

    for (const auto& [sequence, order_id] : index_) {
      const auto it = orders_.find(order_id);
      if (it != orders_.end() &&
          it->second.sequence == sequence &&
          it->second.order) {
        result.push_back(it->second.order);
      }
    }

    return result;
  }

  void snapshot_by_sequence_into(
    std::vector<std::shared_ptr<OrderType>>& result
  ) const {
    std::lock_guard<std::mutex> lock(mutex_);
    result.clear();
    result.reserve(orders_.size());

    for (const auto& [sequence, order_id] : index_) {
      const auto it = orders_.find(order_id);
      if (it != orders_.end() &&
          it->second.sequence == sequence &&
          it->second.order) {
        result.push_back(it->second.order);
      }
    }
  }

  std::vector<std::shared_ptr<OrderType>> to_vec() const {
    return snapshot_vec();
  }

  bool empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.empty();
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return orders_.size();
  }

private:
  struct QueueEntry {
    InsertionSequence sequence;
    std::shared_ptr<OrderType> order;
  };

  static void validate_replacement(
    OrderId order_id,
    const std::shared_ptr<OrderType>& replacement
  ) {
    if (!replacement) {
      throw std::invalid_argument("OrderQueue replacement cannot be null");
    }
    if (replacement->get_id() != order_id) {
      throw std::invalid_argument("OrderQueue replacement ID must match the stored order ID");
    }
  }

  InsertionSequence next_sequence_unlocked() {
    if (next_sequence_ == std::numeric_limits<InsertionSequence>::max()) {
      throw std::overflow_error("OrderQueue insertion sequence exhausted");
    }
    return next_sequence_++;
  }

  std::optional<std::pair<InsertionSequence, std::shared_ptr<OrderType>>>
  pop_entry_unlocked() {
    while (!index_.empty()) {
      const auto index_it = index_.begin();
      const InsertionSequence sequence = index_it->first;
      const OrderId order_id = index_it->second;
      const auto order_it = orders_.find(order_id);

      if (order_it == orders_.end() || order_it->second.sequence != sequence) {
        index_.erase(index_it);
        continue;
      }

      auto order = std::move(order_it->second.order);
      orders_.erase(order_it);
      index_.erase(index_it);
      return std::pair{sequence, std::move(order)};
    }

    return std::nullopt;
  }

  mutable std::mutex mutex_;
  std::unordered_map<OrderId, QueueEntry> orders_;
  std::map<InsertionSequence, OrderId> index_;
  InsertionSequence next_sequence_{0};
};

} // namespace lemon
