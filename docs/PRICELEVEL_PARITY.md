# PriceLevel Parity with Rust

## Audit baseline

This audit compares Lemon at `5fff13b` with
[`joaquinbejar/PriceLevel`](https://github.com/joaquinbejar/PriceLevel) at
`5ee60e66378d5aa861a07e573bee19a6d48b4e97` (the upstream `main` tip inspected
on 2026-07-23). Claims below are based on Lemon's `src/lemon/level.cpp`,
`include/lemon/detail/pricelevel/`, and tests, and the Rust reference's
`src/price_level/`, `src/price_level/tests/`, `CHANGELOG.md`, and `benches/`.

| Capability | Lemon C++ | Rust reference | Planned stage | Notes |
| --- | --- | --- | --- | --- |
| Duplicate-ID admission | Complete | Complete | 0 | `PriceLevel::add_order`; Rust `add_order` |
| Order price validation | Missing | Complete | 3 | Lemon does not compare with `price_` |
| Single-side level invariant | Missing | Complete | 3 | Rust `topology`; Lemon has no side pin |
| Self-match prevention | Assert only | Complete | 1 | Lemon asserts after queue mutation |
| FIFO partial-fill behavior | Complete | Complete | 7 | `FrontAction::KeepInPlace` |
| Replenishment priority | Complete | Complete | 7 | `ReplaceAtTail` |
| Quantity-increase priority loss | Complete, unsafe failure path | Complete | 4 | `update_quantity` mutates before counters |
| IOC / FOK / post-only | Implemented | Complete | 1, 5, 7 | Add adversarial coverage |
| Market-to-limit behavior | Partial | Complete | 9 | Taker kind exists; no conversion result |
| Snapshot checksum | Complete | Complete | 2, 6 | Lemon SHA-256 package v2 |
| Snapshot FIFO restoration | Missing | Complete | 2 | `snapshot()` uses timestamp view |
| Aggregate overflow handling | Partial | Complete | 4 | Own-order total not checked on admission |
| Update exception safety | Missing | Complete | 4 | Queue commits before checked counters |
| Replenishment overflow handling | Missing | Complete | 5 | Queue can commit before visible add |
| Statistics atomicity | Missing | Complete | 6 | Independent atomic updates can tear |
| Statistics degradation indicator | Missing | Complete | 6 | Rust `stats_degraded` |
| Concurrent cancel versus match | Serialized | Linearized | 7, 9 | Lemon `match_mutex_` plus queue mutex |
| Concurrent update versus match | Serialized | Linearized | 7, 9 | Lemon uses a global level mutex |
| Post-only decision linearization | Serialized | Epoch-based | 7, 9 | Correct under Lemon mutex |
| FOK versus concurrent mutators | Serialized | RW-lock exclusion | 7, 9 | Correct under Lemon mutex |
| Level poison/fail-fast behavior | Missing | Complete | 9 | `std::mutex` has no poison state |
| Lock-free ordinary matching | No | Yes | 9 | Intentional architecture gap |
| Text parsing | Missing | Complete | 9 | Only JSON snapshot parsing in Lemon |
| Typed error categories | Missing | Complete | 9 | Lemon uses message-only `PriceLevelError` |
| Price width | 64-bit | 128-bit | 9 | Lemon `value_type` is `uint64_t` |
| Benchmark coverage | Missing | Extensive | 8 | Add equivalent local harnesses |

## Implementation plan

Stages 1–6 address correctness in dependency order: reject self matches, persist
consumption order, enforce topology, make admission/update transitions strongly
exception safe, model replenishment headroom, and transact statistics. Stage 7
adds deterministic invariant and concurrency tests. Stage 8 adds reproducible
optimized C++ and pinned-Rust benchmarks. Stage 9 records measured results and
re-evaluates every remaining semantic, concurrency, API, and persistence gap.
