#!/usr/bin/env python3
"""
find_variant_worse_than_off.py

Scan a REDWOOD wide-format results CSV and report every case where:

    reactive_rescheduling / stop_running_jobs = "variant"

produces a HIGHER error than:

    reactive_rescheduling / stop_running_jobs = "off"

for the same:
    - source CSV row
    - heuristic
    - temporal_redundancy setting

Expected heuristic column format:

    <heuristic>__<temporal_redundancy>__<reactive_rescheduling>

Examples:

    dynamic__independent__off
    dynamic__independent__variant

    static_foresighted_expected_error__dependent__off
    static_foresighted_expected_error__dependent__variant

Outputs:
    1. A detailed CSV with one row per violation.
    2. A summary CSV grouped by heuristic and temporal redundancy.

Usage:

    python find_variant_worse_than_off.py results.csv

Optional:
    --tolerance 1e-9
    --exclude-random
"""

from __future__ import annotations

import argparse
import os
import sys

import pandas as pd


SEP = "__"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Find every REDWOOD result row where reactive rescheduling "
            "variant has higher error than off."
        )
    )

    parser.add_argument(
        "csv",
        help="Wide-format REDWOOD results CSV.",
    )

    parser.add_argument(
        "--output",
        default="variant_worse_than_off.csv",
        help="Detailed violation CSV.",
    )

    parser.add_argument(
        "--summary",
        default="variant_worse_summary.csv",
        help="Summary CSV grouped by heuristic/temporal redundancy.",
    )

    parser.add_argument(
        "--tolerance",
        type=float,
        default=0.0,
        help=(
            "Require variant > off + tolerance. "
            "Default: 0.0."
        ),
    )

    parser.add_argument(
        "--exclude-random",
        action="store_true",
        help="Exclude the random heuristic.",
    )

    return parser.parse_args()


def parse_result_column(column: str):
    """
    Parse:
        heuristic__temporal_redundancy__reactive_rescheduling
    """
    parts = str(column).rsplit(SEP, 2)

    if len(parts) != 3:
        return None

    heuristic, temporal_redundancy, reactive_rescheduling = parts

    if not heuristic or not temporal_redundancy or not reactive_rescheduling:
        return None

    return heuristic, temporal_redundancy, reactive_rescheduling


def find_pairs(columns: list[str], exclude_random: bool):
    """
    Pair:
        <heuristic>__<temporal>__off
    with:
        <heuristic>__<temporal>__variant
    """
    parsed = {}

    for column in columns:
        result = parse_result_column(column)

        if result is None:
            continue

        heuristic, temporal, reactive = result

        if exclude_random and heuristic == "random":
            continue

        if reactive not in {"off", "variant"}:
            continue

        parsed[(heuristic, temporal, reactive)] = column

    pairs = []

    keys = {
        (heuristic, temporal)
        for heuristic, temporal, _reactive in parsed
    }

    for heuristic, temporal in sorted(keys):
        off_col = parsed.get(
            (heuristic, temporal, "off")
        )
        variant_col = parsed.get(
            (heuristic, temporal, "variant")
        )

        if off_col is None or variant_col is None:
            continue

        pairs.append(
            (
                heuristic,
                temporal,
                off_col,
                variant_col,
            )
        )

    return pairs


def main() -> None:
    args = parse_args()

    csv_path = os.path.abspath(args.csv)
    output_path = os.path.abspath(args.output)
    summary_path = os.path.abspath(args.summary)

    if not os.path.exists(csv_path):
        print(
            f"[ERROR] Input CSV does not exist: {csv_path}",
            file=sys.stderr,
        )
        sys.exit(1)

    print(f"[READ] {csv_path}", flush=True)

    df = pd.read_csv(csv_path)

    print(
        f"[READ] rows={len(df):,}, columns={len(df.columns):,}",
        flush=True,
    )

    pairs = find_pairs(
        df.columns.tolist(),
        exclude_random=args.exclude_random,
    )

    if not pairs:
        print(
            "[ERROR] No matching off/variant heuristic pairs found.",
            file=sys.stderr,
        )
        sys.exit(1)

    print(
        f"[PAIR] Found {len(pairs)} off/variant column pairs.",
        flush=True,
    )

    if args.exclude_random:
        print("[PAIR] Random heuristic excluded.", flush=True)

    all_heuristic_columns = {
        col
        for col in df.columns
        if parse_result_column(str(col)) is not None
    }

    metadata_columns = [
        col
        for col in df.columns
        if col not in all_heuristic_columns
    ]

    violations = []

    for (
            heuristic,
            temporal_redundancy,
            off_col,
            variant_col,
    ) in pairs:

        off_values = pd.to_numeric(
            df[off_col],
            errors="coerce",
        )

        variant_values = pd.to_numeric(
            df[variant_col],
            errors="coerce",
        )

        valid = (
                off_values.notna()
                & variant_values.notna()
        )

        worse = (
                valid
                & (
                        variant_values
                        > off_values + args.tolerance
                )
        )

        violation_indices = df.index[worse]

        print(
            f"[CHECK] {heuristic} | {temporal_redundancy}: "
            f"{len(violation_indices):,} variant-worse cases",
            flush=True,
        )

        for idx in violation_indices:
            row = {
                "source_row_index": int(idx),
            }

            for col in metadata_columns:
                row[col] = df.at[idx, col]

            off_value = float(
                off_values.at[idx]
            )
            variant_value = float(
                variant_values.at[idx]
            )

            difference = (
                    variant_value
                    - off_value
            )

            row.update(
                {
                    "heuristic": heuristic,
                    "temporal_redundancy": temporal_redundancy,
                    "off_error": off_value,
                    "variant_error": variant_value,
                    "variant_minus_off": difference,
                    "variant_percent_worse": (
                        difference
                        / abs(off_value)
                        * 100.0
                        if off_value != 0
                        else pd.NA
                    ),
                    "off_column": off_col,
                    "variant_column": variant_col,
                }
            )

            violations.append(row)

    preferred_metadata = [
        "app_config_id",
        "num_nodes",
        "repetition_id",
        "e_fail_multiplier",
        "e_fail",
        "lambda",
        "restart_overhead",
        "deadline",
        "delta_t",
    ]

    detailed_columns = (
            ["source_row_index"]
            + [
                col
                for col in preferred_metadata
                if col in metadata_columns
            ]
            + [
                col
                for col in metadata_columns
                if col not in preferred_metadata
            ]
            + [
                "heuristic",
                "temporal_redundancy",
                "off_error",
                "variant_error",
                "variant_minus_off",
                "variant_percent_worse",
                "off_column",
                "variant_column",
            ]
    )

    if violations:
        detailed = pd.DataFrame(violations)

        detailed = detailed[
            [
                col
                for col in detailed_columns
                if col in detailed.columns
            ]
        ]

        sort_cols = [
            col
            for col in [
                "app_config_id",
                "num_nodes",
                "repetition_id",
                "e_fail_multiplier",
                "heuristic",
                "temporal_redundancy",
            ]
            if col in detailed.columns
        ]

        if sort_cols:
            detailed = detailed.sort_values(
                sort_cols,
                ignore_index=True,
            )

        summary = (
            detailed.groupby(
                [
                    "heuristic",
                    "temporal_redundancy",
                ],
                dropna=False,
                observed=True,
            )
            .agg(
                violation_count=(
                    "variant_minus_off",
                    "size",
                ),
                mean_off_error=(
                    "off_error",
                    "mean",
                ),
                mean_variant_error=(
                    "variant_error",
                    "mean",
                ),
                mean_difference=(
                    "variant_minus_off",
                    "mean",
                ),
                max_difference=(
                    "variant_minus_off",
                    "max",
                ),
                mean_percent_worse=(
                    "variant_percent_worse",
                    "mean",
                ),
            )
            .reset_index()
            .sort_values(
                [
                    "violation_count",
                    "mean_difference",
                ],
                ascending=[
                    False,
                    False,
                ],
                ignore_index=True,
            )
        )

    else:
        detailed = pd.DataFrame(
            columns=detailed_columns
        )

        summary = pd.DataFrame(
            columns=[
                "heuristic",
                "temporal_redundancy",
                "violation_count",
                "mean_off_error",
                "mean_variant_error",
                "mean_difference",
                "max_difference",
                "mean_percent_worse",
            ]
        )

    os.makedirs(
        os.path.dirname(output_path) or ".",
        exist_ok=True,
        )
    os.makedirs(
        os.path.dirname(summary_path) or ".",
        exist_ok=True,
        )

    detailed.to_csv(
        output_path,
        index=False,
    )

    summary.to_csv(
        summary_path,
        index=False,
    )

    print()
    print("=" * 72)
    print("Variant > off check complete")
    print(f"Pairs checked   : {len(pairs):,}")
    print(f"Violation rows  : {len(detailed):,}")
    print(f"Detailed report : {output_path}")
    print(f"Summary report  : {summary_path}")
    print("=" * 72)


if __name__ == "__main__":
    main()
