#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <lemon/lemon.hpp>

namespace {

using Clock = std::chrono::steady_clock;

constexpr lemon::Price benchmark_price{10'000};
constexpr lemon::TimestampMs base_timestamp = 1'716'000'000'000;

struct Options {
  std::uint64_t operations{2'000};
  std::uint64_t depth{512};
  std::uint64_t warmups{1};
  std::uint64_t trials{5};
  std::string json_path;
};

struct Work {
  std::uint64_t operations{};
  std::uint64_t bytes{};
};

struct Result {
  std::string name;
  double median_ops_per_second{};
  double minimum_ops_per_second{};
  double maximum_ops_per_second{};
  double median_bytes_per_second{};
};

std::atomic<std::uint64_t> observed{0};

lemon::OrderType standard_order(
  lemon::OrderId id,
  std::uint64_t quantity,
  lemon::TimestampMs timestamp
) {
  return lemon::OrderType::standard(
    id,
    lemon::Side::Sell,
    benchmark_price,
    lemon::Quantity{quantity},
    timestamp
  );
}

std::uint64_t parse_number(std::string_view value, const char* option) {
  std::size_t position = 0;
  const std::uint64_t result = std::stoull(std::string{value}, &position);
  if (position != value.size() || result == 0) {
    throw std::invalid_argument(std::string{option} + " must be positive");
  }
  return result;
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    auto next = [&](const char* option) -> std::string_view {
      if (++index >= argc) {
        throw std::invalid_argument(std::string{"missing value for "} + option);
      }
      return argv[index];
    };

    if (argument == "--operations") {
      options.operations = parse_number(next("--operations"), "--operations");
    } else if (argument == "--depth") {
      options.depth = parse_number(next("--depth"), "--depth");
    } else if (argument == "--warmup") {
      options.warmups = parse_number(next("--warmup"), "--warmup");
    } else if (argument == "--trials") {
      options.trials = parse_number(next("--trials"), "--trials");
    } else if (argument == "--json") {
      options.json_path = next("--json");
    } else if (argument == "--help") {
      std::cout
        << "Usage: lemon_price_level_benchmark [--operations N] [--depth N] "
           "[--warmup N] [--trials N] [--json PATH]\n";
      std::exit(0);
    } else {
      throw std::invalid_argument("unknown option: " + std::string{argument});
    }
  }
  return options;
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const std::size_t middle = values.size() / 2;
  if (values.size() % 2 == 0) {
    return (values[middle - 1] + values[middle]) / 2.0;
  }
  return values[middle];
}

Result measure(
  std::string name,
  const Options& options,
  const std::function<Work()>& workload
) {
  for (std::uint64_t index = 0; index < options.warmups; ++index) {
    const Work work = workload();
    observed.fetch_add(work.operations + work.bytes, std::memory_order_relaxed);
  }

  std::vector<double> operation_rates;
  std::vector<double> byte_rates;
  operation_rates.reserve(options.trials);
  byte_rates.reserve(options.trials);
  for (std::uint64_t index = 0; index < options.trials; ++index) {
    const auto start = Clock::now();
    const Work work = workload();
    const auto stop = Clock::now();
    const double seconds = std::chrono::duration<double>(stop - start).count();
    if (work.operations == 0 || seconds <= 0.0) {
      throw std::runtime_error("benchmark produced no measurable work");
    }
    operation_rates.push_back(
      static_cast<double>(work.operations) / seconds
    );
    byte_rates.push_back(static_cast<double>(work.bytes) / seconds);
    observed.fetch_add(work.operations + work.bytes, std::memory_order_relaxed);
  }

  return Result{
    std::move(name),
    median(operation_rates),
    *std::min_element(operation_rates.begin(), operation_rates.end()),
    *std::max_element(operation_rates.begin(), operation_rates.end()),
    median(byte_rates)
  };
}

std::vector<Result> run_benchmarks(const Options& options) {
  std::vector<Result> results;

  results.push_back(measure("standard_admission", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      level.add_order(standard_order(index + 1, 1, base_timestamp + index));
    }
    if (level.order_count() != options.operations) {
      throw std::runtime_error("standard admission verification failed");
    }
    return Work{options.operations, 0};
  }));

  results.push_back(measure("fifo_standard_matching", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    for (std::uint64_t index = 0; index < options.depth; ++index) {
      level.add_order(standard_order(index + 1, 1, base_timestamp + index));
    }
    lemon::TradeIdGenerator trade_ids{1};
    const auto result = level.match_order(
      lemon::Quantity{options.depth},
      options.depth + 1,
      {},
      lemon::TakerKind::Standard,
      base_timestamp + options.depth + 1,
      trade_ids
    );
    if (result.trades().size() != options.depth || level.order_count() != 0) {
      throw std::runtime_error("FIFO matching verification failed");
    }
    for (std::uint64_t index = 0; index < options.depth; ++index) {
      if (result.trades().as_vector()[index].maker_order_id() != index + 1) {
        throw std::runtime_error("FIFO trade ordering verification failed");
      }
    }
    return Work{options.depth, 0};
  }));

  results.push_back(measure("partial_fills", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    level.add_order(standard_order(
      1,
      options.operations + 1,
      base_timestamp
    ));
    lemon::TradeIdGenerator trade_ids{1};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      const auto result = level.match_order(
        lemon::Quantity{1},
        index + 2,
        {},
        lemon::TakerKind::Standard,
        base_timestamp + index + 1,
        trade_ids
      );
      if (result.trades().size() != 1 ||
          result.trades().as_vector().front().maker_order_id() != 1) {
        throw std::runtime_error("partial fill priority verification failed");
      }
    }
    return Work{options.operations, 0};
  }));

  results.push_back(measure("iceberg_replenishment", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    const std::uint64_t hidden = options.operations / options.depth + 2;
    for (std::uint64_t index = 0; index < options.depth; ++index) {
      level.add_order(lemon::OrderType::iceberg(
        index + 1,
        lemon::Side::Sell,
        benchmark_price,
        lemon::Quantity{1},
        lemon::Quantity{hidden},
        base_timestamp + index
      ));
    }
    lemon::TradeIdGenerator trade_ids{1};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      const auto result = level.match_order(
        lemon::Quantity{1},
        options.depth + index + 1,
        {},
        lemon::TakerKind::Standard,
        base_timestamp + options.depth + index + 1,
        trade_ids
      );
      if (result.executed_quantity() != lemon::Quantity{1}) {
        throw std::runtime_error("iceberg verification failed");
      }
    }
    return Work{options.operations, 0};
  }));

  results.push_back(measure("cancellation", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      level.add_order(standard_order(index + 1, 10, base_timestamp + index));
    }
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      if (!level.update_order(lemon::OrderUpdate::cancel(index + 1))) {
        throw std::runtime_error("cancellation verification failed");
      }
    }
    return Work{options.operations, 0};
  }));

  auto quantity_updates = [&](bool increase) {
    lemon::PriceLevel level{benchmark_price};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      level.add_order(standard_order(index + 1, 10, base_timestamp + index));
    }
    const lemon::Quantity quantity{increase ? 20ULL : 5ULL};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      if (!level.update_order(
            lemon::OrderUpdate::quantity(index + 1, quantity)
          )) {
        throw std::runtime_error("quantity update verification failed");
      }
    }
    return Work{options.operations, 0};
  };
  results.push_back(measure("quantity_decrease", options, [&] {
    return quantity_updates(false);
  }));
  results.push_back(measure("quantity_increase", options, [&] {
    return quantity_updates(true);
  }));

  auto populate_snapshot_level = [&](lemon::PriceLevel& level) {
    for (std::uint64_t index = 0; index < options.depth; ++index) {
      if (index % 3 == 0) {
        level.add_order(standard_order(
          index + 1, 10, base_timestamp + index
        ));
      } else {
        level.add_order(lemon::OrderType::iceberg(
          index + 1,
          lemon::Side::Sell,
          benchmark_price,
          lemon::Quantity{5},
          lemon::Quantity{15},
          base_timestamp + index
        ));
      }
    }
  };

  results.push_back(measure("snapshot_serialization", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    populate_snapshot_level(level);
    std::uint64_t bytes = 0;
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      const std::string payload = level.snapshot_to_json();
      bytes += payload.size();
    }
    return Work{options.operations, bytes};
  }));

  lemon::PriceLevel restoration_source{benchmark_price};
  populate_snapshot_level(restoration_source);
  const std::string restoration_payload =
    restoration_source.snapshot_to_json();
  results.push_back(measure("snapshot_restoration", options, [&] {
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      const auto restored = lemon::PriceLevel::from_snapshot_json(
        restoration_payload
      );
      if (restored.order_count() != options.depth) {
        throw std::runtime_error("snapshot restoration verification failed");
      }
    }
    return Work{
      options.operations,
      options.operations * restoration_payload.size()
    };
  }));

  results.push_back(measure("mixed_single_thread", options, [&] {
    lemon::PriceLevel level{benchmark_price};
    for (std::uint64_t index = 0; index < options.depth; ++index) {
      level.add_order(standard_order(
        index + 1, 10, base_timestamp + index
      ));
    }
    lemon::OrderId next_id = options.depth + 1;
    lemon::TradeIdGenerator trade_ids{1};
    for (std::uint64_t index = 0; index < options.operations; ++index) {
      const std::uint64_t slot = index % 20;
      if (slot < 8) {
        const lemon::OrderId order_id = next_id++;
        level.add_order(standard_order(
          order_id, 10, base_timestamp + order_id
        ));
      } else if (slot < 13) {
        static_cast<void>(level.match_order(
          lemon::Quantity{1},
          1'000'000 + index,
          {},
          lemon::TakerKind::Standard,
          base_timestamp + 1'000'000 + index,
          trade_ids
        ));
      } else if (slot < 17) {
        const auto orders = level.snapshot_by_insertion_sequence();
        if (!orders.empty()) {
          static_cast<void>(level.update_order(
            lemon::OrderUpdate::cancel(orders.front()->get_id())
          ));
        }
      } else if (slot < 19) {
        const auto orders = level.snapshot_by_insertion_sequence();
        if (!orders.empty()) {
          static_cast<void>(level.update_order(
            lemon::OrderUpdate::quantity(
              orders.front()->get_id(),
              lemon::Quantity{5}
            )
          ));
        }
      } else {
        observed.fetch_add(
          level.snapshot_to_json().size(),
          std::memory_order_relaxed
        );
      }
    }
    return Work{options.operations, 0};
  }));

  return results;
}

void write_json(
  const std::string& path,
  const Options& options,
  const std::vector<Result>& results
) {
  std::ofstream output{path};
  if (!output) {
    throw std::runtime_error("cannot open JSON output: " + path);
  }
  output << std::fixed << std::setprecision(3)
         << "{\n"
         << "  \"implementation\": \"lemon-cpp\",\n"
         << "  \"operations\": " << options.operations << ",\n"
         << "  \"depth\": " << options.depth << ",\n"
         << "  \"warmups\": " << options.warmups << ",\n"
         << "  \"trials\": " << options.trials << ",\n"
         << "  \"results\": [\n";
  for (std::size_t index = 0; index < results.size(); ++index) {
    const auto& result = results[index];
    output << "    {\"name\": \"" << result.name
           << "\", \"median_ops_per_sec\": "
           << result.median_ops_per_second
           << ", \"min_ops_per_sec\": " << result.minimum_ops_per_second
           << ", \"max_ops_per_sec\": " << result.maximum_ops_per_second
           << ", \"median_bytes_per_sec\": "
           << result.median_bytes_per_second << "}";
    output << (index + 1 == results.size() ? "\n" : ",\n");
  }
  output << "  ]\n}\n";
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Options options = parse_options(argc, argv);
    const auto results = run_benchmarks(options);
    std::cout << std::fixed << std::setprecision(0);
    for (const auto& result : results) {
      std::cout << std::left << std::setw(28) << result.name
                << std::right << std::setw(14)
                << result.median_ops_per_second << " ops/s";
      if (result.median_bytes_per_second > 0.0) {
        std::cout << "  " << result.median_bytes_per_second << " bytes/s";
      }
      std::cout << '\n';
    }
    if (!options.json_path.empty()) {
      write_json(options.json_path, options, results);
    }
    return observed.load(std::memory_order_relaxed) == 0 ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark error: " << error.what() << '\n';
    return 1;
  }
}
