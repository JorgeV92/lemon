#!/bin/sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
result_dir="$repo_dir/benchmarks/results"
operations=${LEMON_BENCH_OPERATIONS:-500}
depth=${LEMON_BENCH_DEPTH:-256}
warmups=${LEMON_BENCH_WARMUPS:-1}
trials=${LEMON_BENCH_TRIALS:-5}

mkdir -p "$result_dir"

cmake -S "$repo_dir" -B "$repo_dir/build-release" \
  -DCMAKE_BUILD_TYPE=Release -DLEMON_BUILD_BENCHMARKS=ON
cmake --build "$repo_dir/build-release" --parallel
cargo build --release \
  --manifest-path "$repo_dir/benchmarks/rust_baseline/Cargo.toml"

"$repo_dir/build-release/lemon_price_level_benchmark" \
  --operations "$operations" --depth "$depth" \
  --warmup "$warmups" --trials "$trials" \
  --json "$result_dir/lemon_cpp.json"

"$repo_dir/benchmarks/rust_baseline/target/release/lemon-pricelevel-rust-baseline" \
  --operations "$operations" --depth "$depth" \
  --warmup "$warmups" --trials "$trials" \
  --json "$result_dir/pricelevel_rust.json"

python3 "$script_dir/compare_pricelevel_benchmarks.py" \
  "$result_dir/lemon_cpp.json" \
  "$result_dir/pricelevel_rust.json" \
  "$result_dir/comparison.md"

{
  date -u '+date_utc: %Y-%m-%dT%H:%M:%SZ'
  uname -a
  if command -v system_profiler >/dev/null 2>&1; then
    system_profiler SPHardwareDataType -detailLevel mini
  fi
  c++ --version
  cmake --version
  rustc --version
  cargo --version
  git -C "$repo_dir" rev-parse HEAD
  printf '%s\n' \
    'rust_pricelevel_sha: 5ee60e66378d5aa861a07e573bee19a6d48b4e97' \
    'cpp_build: Release' \
    'rust_profile: release (thin LTO, codegen-units=1)' \
    "command: LEMON_BENCH_OPERATIONS=$operations LEMON_BENCH_DEPTH=$depth LEMON_BENCH_WARMUPS=$warmups LEMON_BENCH_TRIALS=$trials scripts/run_pricelevel_benchmarks.sh"
} > "$result_dir/environment.txt"

printf 'Results written to %s\n' "$result_dir"
