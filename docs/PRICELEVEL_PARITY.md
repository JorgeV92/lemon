# PriceLevel Parity with Rust

## Reference and scope

This final audit compares Lemon through
`8f1511146beb815ae94bf3841b298a36ec4c052f` with the upstream `main` commit
`5ee60e66378d5aa861a07e573bee19a6d48b4e97` of
[`joaquinbejar/PriceLevel`](https://github.com/joaquinbejar/PriceLevel),
inspected on 2026-07-23. Sources reviewed include both implementations,
price-level tests, changelog, and benchmark infrastructure.

## Completed parity

Lemon now rejects duplicate, wrong-price, mixed-side, overflowing, and
self-matching admissions without mutating level state. The first maker pins
the resting side, and draining the level unpins it. FIFO partial fills retain
priority; quantity increases and iceberg/reserve replenishments lose priority.
IOC, FOK, and post-only decisions are serialized with mutation.

Snapshots persist consumption order, validate checksums and topology, reject
duplicates/overflow, and preserve aggregates through JSON and
`PriceLevelData`. Format v3 persists the sticky `stats_degraded` flag and
continues to validate/read v2. Execution statistics update all fields or none,
produce monotonic last-execution timestamps, and retain committed trades when
recording degrades.

These behaviors are covered by focused regressions plus fixed-seed randomized
invariant tests in `tests/price_level_invariant_test.cpp`. ASan/UBSan and TSan
both pass all nine registered tests on the recorded environment.

## Intentional design differences

- Lemon serializes public level mutation with `std::mutex`; Rust uses
  sharded/lock-free structures for ordinary matching and an RW lock for FOK.
- Lemon uses `std::shared_ptr`; Rust uses `Arc`.
- Lemon order/trade IDs and prices are `uint64_t`; Rust supports richer `Id`
  variants, UUID trade IDs, and `u128` prices.
- Lemon reports message-based exceptions; Rust returns typed error variants.
- Lemon's snapshot wire format and SHA-256 payload are project-specific, not
  wire-compatible with Rust.

## Remaining correctness gaps

No gap remains for the task's core admission, topology, self-match, FIFO,
replenishment, update, or statistics invariants. Two exception paths deserve
P0 follow-up: trade-ID exhaustion and allocation failure can occur after a
queue match decision has committed, and the current level has no poisoned-state
fail-fast mechanism after an unexpected mid-operation failure. Legacy snapshots
written before FIFO persistence was fixed cannot reconstruct priority that was
already serialized in timestamp order.

Market-to-limit is equivalent only at the price-level boundary: both levels
fill available liquidity and return a remainder; converting/resting that
remainder remains the surrounding order book's responsibility.

## Remaining concurrency gaps

- **Lock-free GTC/IOC/Day path:** absent; Lemon takes `match_mutex_`.
- **Match versus cancel and update:** linearized correctly by the same mutex,
  but with level-wide contention rather than Rust's per-entry protocol.
- **FOK versus mutators:** fully excluded by the global mutex.
- **Post-only decision epoch:** unnecessary under current serialization; a
  lock-free redesign would require an epoch/retry protocol.
- **Topology epoch:** unnecessary because snapshot, drain, and admission share
  the level mutex.
- **Poisoned-level fail-fast:** absent.
- **Single logical matcher:** multiple caller threads are safe, but execute as
  one serialized logical matcher.

Reducing contention must preserve these linearization guarantees. A lock-free
rewrite is not justified before profiling representative concurrent workloads.

## Remaining API and persistence gaps

Lemon lacks Rust's text `FromStr` parsing and generic serde integration.
`PriceLevelError` has no typed categories. Numeric IDs cannot represent Rust's
UUID/ULID variants, and 64-bit prices are narrower than Rust's 128-bit prices.
Snapshot migration supports v2 to v3 only; v1 is rejected. The buffer-reuse
`snapshot_by_sequence_into()` avoids reallocating the caller's vector but still
materializes `shared_ptr` references; there is no callback/range-based
non-allocating iterator.

## Benchmark conclusions

The recorded 500-operation, depth-256, five-trial Release run is reproducible
with `scripts/run_pricelevel_benchmarks.sh`. Lemon led all seven
mutation-focused workloads (1.24× to 4.18× in this run). Rust led snapshot
serialization (1.56×), restoration (1.85×), and the mixed workload (1.85×).

These are single-machine microbenchmarks across different languages, ID/value
representations, allocators, and synchronization designs. The Rust ordinary
match architecture is lock-free/sharded while Lemon serializes more work.
No contended result is reported because an equivalent cross-language schedule
has not yet been established. See `benchmarks/results/` for raw data and exact
environment details.

## Recommended next work

1. **P0 — remaining correctness:** make trade creation/ID allocation
   transaction-safe and add level poison/fail-fast recovery.
2. **P1 — exception safety and invariants:** add deterministic allocation and
   sequence-exhaustion fault injection.
3. **P2 — concurrency correctness:** model per-entry match/cancel/update and
   post-only/FOK/topology epochs before changing locks.
4. **P3 — contention reduction:** benchmark representative multithreaded
   schedules and narrow lock scope only where measured.
5. **P4 — lock-free redesign:** consider only after P2 proofs and P3 profiles.
6. **P5 — API/persistence:** typed errors, text parsing, wider prices/IDs,
   additional serializers, and longer migration support.
