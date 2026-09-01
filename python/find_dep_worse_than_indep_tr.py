#!/usr/bin/env python3
import argparse
import os
import sys
import pandas as pd

SEP = "__"

def parse_args():
    p = argparse.ArgumentParser(
        description="Find rows where dependent temporal redundancy has higher error than independent."
    )
    p.add_argument("csv", help="Wide-format REDWOOD results CSV")
    p.add_argument("--output", default="dependent_worse_than_independent.csv")
    p.add_argument("--summary", default="dependent_worse_summary.csv")
    p.add_argument("--tolerance", type=float, default=0.0)
    return p.parse_args()

def parse_result_col(col):
    parts = str(col).rsplit(SEP, 2)
    if len(parts) != 3:
        return None
    return tuple(parts)

def main():
    args = parse_args()
    csv_path = os.path.abspath(args.csv)

    if not os.path.exists(csv_path):
        print(f"[ERROR] Missing CSV: {csv_path}", file=sys.stderr)
        sys.exit(1)

    df = pd.read_csv(csv_path)

    parsed = {}
    heuristic_cols = set()

    for col in df.columns:
        p = parse_result_col(col)
        if p is None:
            continue

        heuristic, temporal, reactive = p
        heuristic_cols.add(col)

        if heuristic == "random":
            continue
        if temporal not in {"independent", "dependent"}:
            continue

        parsed[(heuristic, reactive, temporal)] = col

    pair_keys = sorted({
        (heuristic, reactive)
        for heuristic, reactive, temporal in parsed
        if (heuristic, reactive, "independent") in parsed
           and (heuristic, reactive, "dependent") in parsed
    })

    if not pair_keys:
        raise RuntimeError("No matching independent/dependent column pairs found.")

    metadata_cols = [
        col for col in df.columns
        if col not in heuristic_cols
    ]

    records = []

    for heuristic, reactive in pair_keys:
        ind_col = parsed[(heuristic, reactive, "independent")]
        dep_col = parsed[(heuristic, reactive, "dependent")]

        ind = pd.to_numeric(df[ind_col], errors="coerce")
        dep = pd.to_numeric(df[dep_col], errors="coerce")

        mask = (
                ind.notna()
                & dep.notna()
                & (dep > ind + args.tolerance)
        )

        indices = df.index[mask]

        print(
            f"{heuristic} | {reactive}: {len(indices)} dependent-worse cases",
            flush=True,
        )

        for idx in indices:
            ind_val = float(ind.at[idx])
            dep_val = float(dep.at[idx])
            diff = dep_val - ind_val

            rec = {
                "source_row_index": int(idx),
            }

            for col in metadata_cols:
                rec[col] = df.at[idx, col]

            rec.update({
                "heuristic": heuristic,
                "reactive_rescheduling": reactive,
                "independent_error": ind_val,
                "dependent_error": dep_val,
                "dependent_minus_independent": diff,
                "dependent_percent_worse": (
                    diff / abs(ind_val) * 100.0
                    if ind_val != 0 else pd.NA
                ),
                "independent_column": ind_col,
                "dependent_column": dep_col,
            })

            records.append(rec)

    detailed = pd.DataFrame(records)

    preferred = [
        "source_row_index",
        "app_config_id",
        "num_nodes",
        "repetition_id",
        "e_fail_multiplier",
        "e_fail",
        "lambda",
        "restart_overhead",
        "deadline",
        "delta_t",
        "heuristic",
        "reactive_rescheduling",
        "independent_error",
        "dependent_error",
        "dependent_minus_independent",
        "dependent_percent_worse",
        "independent_column",
        "dependent_column",
    ]

    if detailed.empty:
        detailed = pd.DataFrame(columns=preferred)
        summary = pd.DataFrame(columns=[
            "heuristic",
            "reactive_rescheduling",
            "violation_count",
            "mean_independent_error",
            "mean_dependent_error",
            "mean_difference",
            "max_difference",
            "mean_percent_worse",
        ])
    else:
        ordered = [c for c in preferred if c in detailed.columns]
        ordered += [c for c in detailed.columns if c not in ordered]
        detailed = detailed[ordered]

        sort_cols = [
            c for c in [
                "app_config_id",
                "num_nodes",
                "repetition_id",
                "e_fail_multiplier",
                "heuristic",
                "reactive_rescheduling",
            ]
            if c in detailed.columns
        ]
        if sort_cols:
            detailed = detailed.sort_values(sort_cols, ignore_index=True)

        summary = (
            detailed.groupby(
                ["heuristic", "reactive_rescheduling"],
                dropna=False,
                observed=True,
            )
            .agg(
                violation_count=("dependent_minus_independent", "size"),
                mean_independent_error=("independent_error", "mean"),
                mean_dependent_error=("dependent_error", "mean"),
                mean_difference=("dependent_minus_independent", "mean"),
                max_difference=("dependent_minus_independent", "max"),
                mean_percent_worse=("dependent_percent_worse", "mean"),
            )
            .reset_index()
            .sort_values(
                ["violation_count", "mean_difference"],
                ascending=[False, False],
                ignore_index=True,
            )
        )

    detailed.to_csv(args.output, index=False)
    summary.to_csv(args.summary, index=False)

    print()
    print(f"Pairs checked : {len(pair_keys)}")
    print(f"Violations    : {len(detailed)}")
    print(f"Detailed CSV  : {os.path.abspath(args.output)}")
    print(f"Summary CSV   : {os.path.abspath(args.summary)}")

if __name__ == "__main__":
    main()
