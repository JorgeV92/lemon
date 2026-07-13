#include <cassert>
#include <memory>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

#include <lemon/lemon.hpp>

namespace {

std::shared_ptr<lemon::OrderType> make_order(
  lemon::OrderId id,
  std::uint64_t quantity,
  lemon::TimestampMs timestamp = 1'717'286'400'000
) {
  return std::make_shared<lemon::OrderType>(
    id,
    lemon::Side::Buy,
    lemon::Price{100},
    lemon::Quantity{quantity},
    lemon::Quantity{quantity},
    lemon::Quantity::zero(),
    timestamp
  );
}

std::vector<lemon::OrderId> ids(
  const std::vector<std::shared_ptr<lemon::OrderType>>& orders
) {
  std::vector<lemon::OrderId> result;
  result.reserve(orders.size());
  for (const auto& order : orders) {
    result.push_back(order->get_id());
  }
  return result;
}

void pop_removes_from_every_index() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));

  const auto first = queue.pop_entry();
  assert(first.has_value());
  assert(first->first == 0);
  assert(first->second->get_id() == 1);
  assert(!queue.find(1).has_value());
  assert(queue.size() == 1);
  assert(ids(queue.snapshot_vec()) == std::vector<lemon::OrderId>{2});

  const auto second = queue.pop();
  assert(second.has_value());
  assert((*second)->get_id() == 2);
  assert(!queue.find(2).has_value());
  assert(queue.empty());
  assert(queue.snapshot_vec().empty());
  assert(!queue.pop().has_value());
}

void remove_cleans_lookup_and_priority_indexes() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));
  queue.push(make_order(3, 30));

  const auto removed = queue.remove(2);
  assert(removed.has_value());
  assert((*removed)->get_id() == 2);
  assert(!queue.find(2).has_value());
  assert(queue.size() == 2);
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{1, 3}));
  assert(!queue.remove(2).has_value());
}

void in_place_update_keeps_priority() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));
  queue.push(make_order(3, 30));

  const auto previous = queue.update_in_place(2, make_order(2, 200));
  assert(previous.has_value());
  assert((*previous)->get_quantity() == lemon::Quantity{20});
  assert(queue.find(2).value()->get_quantity() == lemon::Quantity{200});
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{1, 2, 3}));

  assert(queue.pop().value()->get_id() == 1);
  assert(queue.pop().value()->get_id() == 2);
  assert(queue.pop().value()->get_id() == 3);
}

void tail_replacement_loses_priority() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));
  queue.push(make_order(3, 30));

  const auto previous = queue.replace_at_tail(1, make_order(1, 100));
  assert(previous.has_value());
  assert((*previous)->get_quantity() == lemon::Quantity{10});
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{2, 3, 1}));
  assert(queue.find(1).value()->get_quantity() == lemon::Quantity{100});
}

void timestamp_snapshot_uses_sequence_as_tie_breaker() {
  lemon::OrderQueue queue;
  constexpr lemon::TimestampMs same_timestamp = 1'717'286'400'000;
  queue.push(make_order(7, 10, same_timestamp));
  queue.push(make_order(2, 20, same_timestamp));
  queue.push(make_order(9, 30, same_timestamp));

  assert(ids(queue.snapshot_vec()) ==
         (std::vector<lemon::OrderId>{7, 2, 9}));
}

void pushing_an_existing_id_replaces_it_at_the_tail() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));
  queue.push(make_order(1, 100));

  assert(queue.size() == 2);
  assert(queue.find(1).value()->get_quantity() == lemon::Quantity{100});
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{2, 1}));
}

void match_front_commits_mutation_under_the_queue_lock() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));
  queue.push(make_order(2, 20));
  std::unordered_set<lemon::InsertionSequence> set_aside;

  const auto kept = queue.match_front(
    set_aside,
    [](lemon::InsertionSequence, const lemon::OrderType& order) {
      return std::pair{
        lemon::FrontAction::keep_in_place(make_order(order.get_id(), 5)),
        order.get_id()
      };
    }
  );
  assert(kept == 1);
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{1, 2}));
  assert(queue.find(1).value()->get_quantity() == lemon::Quantity{5});

  const auto moved = queue.match_front(
    set_aside,
    [](lemon::InsertionSequence, const lemon::OrderType& order) {
      return std::pair{
        lemon::FrontAction::replace_at_tail(make_order(order.get_id(), 4)),
        order.get_id()
      };
    }
  );
  assert(moved == 1);
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{2, 1}));

  const auto removed = queue.match_front(
    set_aside,
    [](lemon::InsertionSequence, const lemon::OrderType& order) {
      return std::pair{lemon::FrontAction::remove(), order.get_id()};
    }
  );
  assert(removed == 2);
  assert(!queue.find(2).has_value());
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{1}));
}

void match_front_can_set_aside_a_non_progressing_order() {
  lemon::OrderQueue queue;
  queue.push(make_order(10, 10));
  queue.push(make_order(11, 11));
  std::unordered_set<lemon::InsertionSequence> set_aside;

  const auto first = queue.match_front(
    set_aside,
    [](lemon::InsertionSequence, const lemon::OrderType& order) {
      return std::pair{lemon::FrontAction::set_aside(), order.get_id()};
    }
  );
  assert(first == 10);
  assert(set_aside.size() == 1);

  const auto second = queue.match_front(
    set_aside,
    [](lemon::InsertionSequence, const lemon::OrderType& order) {
      return std::pair{
        lemon::FrontAction::keep_in_place(make_order(
          order.get_id(),
          order.get_quantity().value()
        )),
        order.get_id()
      };
    }
  );
  assert(second == 11);
  assert(ids(queue.snapshot_by_sequence()) ==
         (std::vector<lemon::OrderId>{10, 11}));
}

void replacement_must_keep_the_same_id() {
  lemon::OrderQueue queue;
  queue.push(make_order(1, 10));

  bool threw = false;
  try {
    static_cast<void>(queue.update_in_place(1, make_order(2, 20)));
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  assert(threw);
  assert(queue.find(1).has_value());
  assert(!queue.find(2).has_value());
}

} // namespace

int main() {
  pop_removes_from_every_index();
  remove_cleans_lookup_and_priority_indexes();
  in_place_update_keeps_priority();
  tail_replacement_loses_priority();
  timestamp_snapshot_uses_sequence_as_tie_breaker();
  pushing_an_existing_id_replaces_it_at_the_tail();
  match_front_commits_mutation_under_the_queue_lock();
  match_front_can_set_aside_a_non_progressing_order();
  replacement_must_keep_the_same_id();
  return 0;
}
