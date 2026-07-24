use pricelevel::{
    Hash32, Id, OrderType, OrderUpdate, Price, PriceLevel, Quantity, Side, TakerKind, TimeInForce,
    TimestampMs, UuidGenerator,
};
use std::env;
use std::fs;
use std::hint::black_box;
use std::time::Instant;
use uuid::Uuid;

const PRICE: u128 = 10_000;
const BASE_TIMESTAMP: u64 = 1_716_000_000_000;

#[derive(Clone)]
struct Options {
    operations: u64,
    depth: u64,
    warmups: u64,
    trials: u64,
    json_path: Option<String>,
}

#[derive(Clone, Copy)]
struct Work {
    operations: u64,
    bytes: u64,
}

struct ResultRow {
    name: &'static str,
    median_ops_per_sec: f64,
    min_ops_per_sec: f64,
    max_ops_per_sec: f64,
    median_bytes_per_sec: f64,
}

fn parse_positive(text: &str, option: &str) -> u64 {
    let value = text
        .parse::<u64>()
        .unwrap_or_else(|_| panic!("{option} must be a positive integer"));
    assert!(value > 0, "{option} must be positive");
    value
}

fn parse_options() -> Options {
    let mut options = Options {
        operations: 2_000,
        depth: 512,
        warmups: 1,
        trials: 5,
        json_path: None,
    };
    let arguments: Vec<String> = env::args().collect();
    let mut index = 1;
    while index < arguments.len() {
        let next = |index: &mut usize, option: &str| -> String {
            *index += 1;
            arguments
                .get(*index)
                .unwrap_or_else(|| panic!("missing value for {option}"))
                .clone()
        };
        match arguments[index].as_str() {
            "--operations" => {
                options.operations =
                    parse_positive(&next(&mut index, "--operations"), "--operations");
            }
            "--depth" => {
                options.depth = parse_positive(&next(&mut index, "--depth"), "--depth");
            }
            "--warmup" => {
                options.warmups = parse_positive(&next(&mut index, "--warmup"), "--warmup");
            }
            "--trials" => {
                options.trials = parse_positive(&next(&mut index, "--trials"), "--trials");
            }
            "--json" => options.json_path = Some(next(&mut index, "--json")),
            "--help" => {
                println!(
                    "Usage: rust_baseline [--operations N] [--depth N] \
                     [--warmup N] [--trials N] [--json PATH]"
                );
                std::process::exit(0);
            }
            other => panic!("unknown option: {other}"),
        }
        index += 1;
    }
    options
}

fn standard_order(id: u64, quantity: u64, timestamp: u64) -> OrderType<()> {
    OrderType::Standard {
        id: Id::from_u64(id),
        price: Price::new(PRICE),
        quantity: Quantity::new(quantity),
        side: Side::Sell,
        user_id: Hash32::zero(),
        timestamp: TimestampMs::new(timestamp),
        time_in_force: TimeInForce::Gtc,
        extra_fields: (),
    }
}

fn iceberg_order(id: u64, visible: u64, hidden: u64, timestamp: u64) -> OrderType<()> {
    OrderType::IcebergOrder {
        id: Id::from_u64(id),
        price: Price::new(PRICE),
        visible_quantity: Quantity::new(visible),
        hidden_quantity: Quantity::new(hidden),
        side: Side::Sell,
        user_id: Hash32::zero(),
        timestamp: TimestampMs::new(timestamp),
        time_in_force: TimeInForce::Gtc,
        extra_fields: (),
    }
}

fn generator() -> UuidGenerator {
    UuidGenerator::new(Uuid::from_u128(1))
}

fn median(mut values: Vec<f64>) -> f64 {
    values.sort_by(f64::total_cmp);
    let middle = values.len() / 2;
    if values.len().is_multiple_of(2) {
        (values[middle - 1] + values[middle]) / 2.0
    } else {
        values[middle]
    }
}

fn measure<F>(name: &'static str, options: &Options, workload: F) -> ResultRow
where
    F: Fn() -> Work,
{
    for _ in 0..options.warmups {
        black_box(workload());
    }
    let mut operation_rates = Vec::with_capacity(options.trials as usize);
    let mut byte_rates = Vec::with_capacity(options.trials as usize);
    for _ in 0..options.trials {
        let start = Instant::now();
        let work = workload();
        let seconds = start.elapsed().as_secs_f64();
        assert!(work.operations > 0 && seconds > 0.0);
        operation_rates.push(work.operations as f64 / seconds);
        byte_rates.push(work.bytes as f64 / seconds);
        black_box(work);
    }
    ResultRow {
        name,
        median_ops_per_sec: median(operation_rates.clone()),
        min_ops_per_sec: operation_rates.iter().copied().fold(f64::INFINITY, f64::min),
        max_ops_per_sec: operation_rates.iter().copied().fold(0.0, f64::max),
        median_bytes_per_sec: median(byte_rates),
    }
}

fn populate_snapshot_level(level: &PriceLevel, depth: u64) {
    for index in 0..depth {
        let order = if index % 3 == 0 {
            standard_order(index + 1, 10, BASE_TIMESTAMP + index)
        } else {
            iceberg_order(index + 1, 5, 15, BASE_TIMESTAMP + index)
        };
        level.add_order(order).expect("snapshot setup admission");
    }
}

fn run(options: &Options) -> Vec<ResultRow> {
    let mut results = Vec::new();

    results.push(measure("standard_admission", options, || {
        let level = PriceLevel::new(PRICE);
        for index in 0..options.operations {
            level
                .add_order(standard_order(index + 1, 1, BASE_TIMESTAMP + index))
                .expect("standard admission");
        }
        assert_eq!(level.order_count(), options.operations as usize);
        Work {
            operations: options.operations,
            bytes: 0,
        }
    }));

    results.push(measure("fifo_standard_matching", options, || {
        let level = PriceLevel::new(PRICE);
        for index in 0..options.depth {
            level
                .add_order(standard_order(index + 1, 1, BASE_TIMESTAMP + index))
                .expect("FIFO setup admission");
        }
        let result = level.match_order(
            options.depth,
            Id::from_u64(options.depth + 1),
            TimeInForce::Gtc,
            TakerKind::Standard,
            TimestampMs::new(BASE_TIMESTAMP + options.depth + 1),
            &generator(),
        );
        assert_eq!(result.trades().len(), options.depth as usize);
        for (index, trade) in result.trades().as_vec().iter().enumerate() {
            assert_eq!(trade.maker_order_id(), Id::from_u64(index as u64 + 1));
        }
        Work {
            operations: options.depth,
            bytes: 0,
        }
    }));

    results.push(measure("partial_fills", options, || {
        let level = PriceLevel::new(PRICE);
        level
            .add_order(standard_order(1, options.operations + 1, BASE_TIMESTAMP))
            .expect("partial setup admission");
        let ids = generator();
        for index in 0..options.operations {
            let result = level.match_order(
                1,
                Id::from_u64(index + 2),
                TimeInForce::Gtc,
                TakerKind::Standard,
                TimestampMs::new(BASE_TIMESTAMP + index + 1),
                &ids,
            );
            assert_eq!(result.trades().len(), 1);
            assert_eq!(result.trades().as_vec()[0].maker_order_id(), Id::from_u64(1));
        }
        Work {
            operations: options.operations,
            bytes: 0,
        }
    }));

    results.push(measure("iceberg_replenishment", options, || {
        let level = PriceLevel::new(PRICE);
        let hidden = options.operations / options.depth + 2;
        for index in 0..options.depth {
            level
                .add_order(iceberg_order(index + 1, 1, hidden, BASE_TIMESTAMP + index))
                .expect("iceberg setup admission");
        }
        let ids = generator();
        for index in 0..options.operations {
            let result = level.match_order(
                1,
                Id::from_u64(options.depth + index + 1),
                TimeInForce::Gtc,
                TakerKind::Standard,
                TimestampMs::new(BASE_TIMESTAMP + options.depth + index + 1),
                &ids,
            );
            assert_eq!(result.executed_quantity().expect("quantity").as_u64(), 1);
        }
        Work {
            operations: options.operations,
            bytes: 0,
        }
    }));

    results.push(measure("cancellation", options, || {
        let level = PriceLevel::new(PRICE);
        for index in 0..options.operations {
            level
                .add_order(standard_order(index + 1, 10, BASE_TIMESTAMP + index))
                .expect("cancel setup admission");
        }
        for index in 0..options.operations {
            assert!(
                level
                    .update_order(OrderUpdate::Cancel {
                        order_id: Id::from_u64(index + 1),
                    })
                    .expect("cancel")
                    .is_some()
            );
        }
        Work {
            operations: options.operations,
            bytes: 0,
        }
    }));

    for (name, quantity) in [("quantity_decrease", 5), ("quantity_increase", 20)] {
        results.push(measure(name, options, || {
            let level = PriceLevel::new(PRICE);
            for index in 0..options.operations {
                level
                    .add_order(standard_order(index + 1, 10, BASE_TIMESTAMP + index))
                    .expect("update setup admission");
            }
            for index in 0..options.operations {
                assert!(
                    level
                        .update_order(OrderUpdate::UpdateQuantity {
                            order_id: Id::from_u64(index + 1),
                            new_quantity: Quantity::new(quantity),
                        })
                        .expect("quantity update")
                        .is_some()
                );
            }
            Work {
                operations: options.operations,
                bytes: 0,
            }
        }));
    }

    results.push(measure("snapshot_serialization", options, || {
        let level = PriceLevel::new(PRICE);
        populate_snapshot_level(&level, options.depth);
        let mut bytes = 0;
        for _ in 0..options.operations {
            bytes += level.snapshot_to_json().expect("snapshot JSON").len() as u64;
        }
        Work {
            operations: options.operations,
            bytes,
        }
    }));

    let source = PriceLevel::new(PRICE);
    populate_snapshot_level(&source, options.depth);
    let restoration_payload = source.snapshot_to_json().expect("restoration payload");
    results.push(measure("snapshot_restoration", options, || {
        for _ in 0..options.operations {
            let restored =
                PriceLevel::from_snapshot_json(&restoration_payload).expect("snapshot restore");
            assert_eq!(restored.order_count(), options.depth as usize);
            black_box(restored);
        }
        Work {
            operations: options.operations,
            bytes: options.operations * restoration_payload.len() as u64,
        }
    }));

    results.push(measure("mixed_single_thread", options, || {
        let level = PriceLevel::new(PRICE);
        for index in 0..options.depth {
            level
                .add_order(standard_order(index + 1, 10, BASE_TIMESTAMP + index))
                .expect("mixed setup admission");
        }
        let mut next_id = options.depth + 1;
        let ids = generator();
        for index in 0..options.operations {
            match index % 20 {
                0..=7 => {
                    let id = next_id;
                    next_id += 1;
                    level
                        .add_order(standard_order(id, 10, BASE_TIMESTAMP + id))
                        .expect("mixed admission");
                }
                8..=12 => {
                    black_box(level.match_order(
                        1,
                        Id::from_u64(1_000_000 + index),
                        TimeInForce::Gtc,
                        TakerKind::Standard,
                        TimestampMs::new(BASE_TIMESTAMP + 1_000_000 + index),
                        &ids,
                    ));
                }
                13..=16 => {
                    if let Some(order) = level.snapshot_by_insertion_seq().first() {
                        black_box(
                            level
                                .update_order(OrderUpdate::Cancel {
                                    order_id: order.id(),
                                })
                                .expect("mixed cancel"),
                        );
                    }
                }
                17..=18 => {
                    if let Some(order) = level.snapshot_by_insertion_seq().first() {
                        black_box(
                            level
                                .update_order(OrderUpdate::UpdateQuantity {
                                    order_id: order.id(),
                                    new_quantity: Quantity::new(5),
                                })
                                .expect("mixed resize"),
                        );
                    }
                }
                _ => {
                    black_box(level.snapshot_to_json().expect("mixed snapshot"));
                }
            }
        }
        Work {
            operations: options.operations,
            bytes: 0,
        }
    }));

    results
}

fn write_json(path: &str, options: &Options, results: &[ResultRow]) {
    let mut output = format!(
        "{{\n  \"implementation\": \"pricelevel-rust\",\n  \"operations\": {},\n  \
         \"depth\": {},\n  \"warmups\": {},\n  \"trials\": {},\n  \"results\": [\n",
        options.operations, options.depth, options.warmups, options.trials
    );
    for (index, result) in results.iter().enumerate() {
        output.push_str(&format!(
            "    {{\"name\": \"{}\", \"median_ops_per_sec\": {:.3}, \
             \"min_ops_per_sec\": {:.3}, \"max_ops_per_sec\": {:.3}, \
             \"median_bytes_per_sec\": {:.3}}}{}\n",
            result.name,
            result.median_ops_per_sec,
            result.min_ops_per_sec,
            result.max_ops_per_sec,
            result.median_bytes_per_sec,
            if index + 1 == results.len() { "" } else { "," }
        ));
    }
    output.push_str("  ]\n}\n");
    fs::write(path, output).expect("write JSON output");
}

fn main() {
    let options = parse_options();
    let results = run(&options);
    for result in &results {
        println!(
            "{:<28}{:>14.0} ops/s{}",
            result.name,
            result.median_ops_per_sec,
            if result.median_bytes_per_sec > 0.0 {
                format!("  {:.0} bytes/s", result.median_bytes_per_sec)
            } else {
                String::new()
            }
        );
    }
    if let Some(path) = &options.json_path {
        write_json(path, &options, &results);
    }
}
