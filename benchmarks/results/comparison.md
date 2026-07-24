# Lemon C++ / Rust PriceLevel benchmark comparison

Configuration: operations=500, depth=256, warmups=1, trials=5. Values are median operations/second.

| Workload | Lemon C++ | Rust PriceLevel | C++ / Rust |
| --- | ---: | ---: | ---: |
| `standard_admission` | 5,736,116 | 4,629,630 | 1.24× |
| `fifo_standard_matching` | 4,360,564 | 1,915,809 | 2.28× |
| `partial_fills` | 7,100,659 | 2,969,562 | 2.39× |
| `iceberg_replenishment` | 3,105,590 | 743,034 | 4.18× |
| `cancellation` | 4,325,858 | 1,218,398 | 3.55× |
| `quantity_decrease` | 4,194,349 | 3,379,315 | 1.24× |
| `quantity_increase` | 2,897,157 | 2,129,925 | 1.36× |
| `snapshot_serialization` | 517 | 805 | 0.64× |
| `snapshot_restoration` | 920 | 1,704 | 0.54× |
| `mixed_single_thread` | 11,115 | 20,542 | 0.54× |

A ratio above 1 means Lemon completed more operations per second in this run. These are language/runtime-specific microbenchmarks, not universal production order-book results.
