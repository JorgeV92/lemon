# PriceLevel Benchmarks

The dependency-free C++ harness and Rust baseline run the same ten logical
single-thread workloads: standard admission, FIFO matching, partial fills,
iceberg replenishment, cancellation, quantity decreases, quantity increases,
snapshot serialization, snapshot restoration, and a mixed workload.

The mixed workload repeats a 20-operation cycle: 40% add, 25% match, 20%
cancel, 10% resize, and 5% snapshot. IDs, quantities, prices, and timestamps
are deterministic, and each workload validates its result outside or alongside
the timed operations. Every run performs configurable warmups and reports the
median, minimum, and maximum throughput across measured trials.

Run both optimized harnesses and generate the comparison:

```sh
scripts/run_pricelevel_benchmarks.sh
```

Tune the run with `LEMON_BENCH_OPERATIONS`, `LEMON_BENCH_DEPTH`,
`LEMON_BENCH_WARMUPS`, and `LEMON_BENCH_TRIALS`. Machine-readable JSON,
environment metadata, and the generated Markdown table are written to
`benchmarks/results/`.

The Rust harness pins
`joaquinbejar/PriceLevel@5ee60e66378d5aa861a07e573bee19a6d48b4e97`
in `Cargo.toml` and `Cargo.lock`. It uses the Cargo release profile with thin
LTO and one codegen unit; Lemon uses CMake's Release configuration.

These measurements compare different languages, value/ID representations,
allocators, and synchronization designs. Rust uses sharded/lock-free structures
on ordinary match paths, while Lemon serializes level mutation with a mutex.
No contended comparison is reported because the current harness does not yet
establish an equivalent multithreaded operation schedule for both
implementations. Microbenchmark results from one machine are not universal
production order-book performance claims.
