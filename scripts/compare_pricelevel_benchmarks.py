#!/usr/bin/env python3

import json
import pathlib
import sys


def load(path: str) -> dict:
    with open(path, encoding="utf-8") as source:
        return json.load(source)


def by_name(document: dict) -> dict:
    return {row["name"]: row for row in document["results"]}


def main() -> int:
    if len(sys.argv) != 4:
        print(
            "usage: compare_pricelevel_benchmarks.py "
            "CPP.json RUST.json OUTPUT.md",
            file=sys.stderr,
        )
        return 2

    cpp = load(sys.argv[1])
    rust = load(sys.argv[2])
    if any(
        cpp[key] != rust[key]
        for key in ("operations", "depth", "warmups", "trials")
    ):
        raise SystemExit("benchmark configurations do not match")

    cpp_rows = by_name(cpp)
    rust_rows = by_name(rust)
    if cpp_rows.keys() != rust_rows.keys():
        raise SystemExit("benchmark workload sets do not match")

    lines = [
        "# Lemon C++ / Rust PriceLevel benchmark comparison",
        "",
        (
            f"Configuration: operations={cpp['operations']}, "
            f"depth={cpp['depth']}, warmups={cpp['warmups']}, "
            f"trials={cpp['trials']}. Values are median operations/second."
        ),
        "",
        "| Workload | Lemon C++ | Rust PriceLevel | C++ / Rust |",
        "| --- | ---: | ---: | ---: |",
    ]
    for name, cpp_row in cpp_rows.items():
        cpp_rate = cpp_row["median_ops_per_sec"]
        rust_rate = rust_rows[name]["median_ops_per_sec"]
        ratio = cpp_rate / rust_rate
        lines.append(
            f"| `{name}` | {cpp_rate:,.0f} | {rust_rate:,.0f} | {ratio:.2f}× |"
        )

    lines.extend(
        [
            "",
            "A ratio above 1 means Lemon completed more operations per second "
            "in this run. These are language/runtime-specific microbenchmarks, "
            "not universal production order-book results.",
            "",
        ]
    )
    pathlib.Path(sys.argv[3]).write_text("\n".join(lines), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
