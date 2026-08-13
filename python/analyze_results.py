#!/usr/bin/env python3

"""
analyze_results.py

Convert a wide REDWOOD-style results CSV into a human-readable long format,
summarize repetitions, compare heuristics by node count and hack configuration,
and optionally generate plots.

Expected heuristic column format:

    <heuristic>__<temporal_redundancy>__<reactive_rescheduling>

Examples:

    dynamic__independent__variant
    random__dependent__aggressive
    static_foresighted_expected_error__aggressive__variant
    greedy_foresighted_incrementing_probability_success__dependent__aggressive

Requirements:
    pip install pandas matplotlib

Examples:
    python analyze_results.py results.csv

    python analyze_results.py results.csv \
        --filter e_fail_multiplier=2

    python analyze_results.py results.csv \
        --filter e_fail_multiplier=2 \
        --filter deadline=10000 \
        --plots

    python analyze_results.py results.csv \
        --filter family=greedy \
        --filter comparator=expected_error
"""

from __future__ import annotations

import argparse
import math
import re
from pathlib import Path

import pandas as pd


HACK_SEPARATOR = "__"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Normalize a wide simulation-results CSV and compare heuristics "
            "by node count and hack configuration."
        )
    )

    parser.add_argument(
        "csv_file",
        type=Path,
        help="Input CSV file.",
    )

    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=Path("analysis_output"),
        help="Directory for generated files (default: analysis_output).",
    )

    parser.add_argument(
        "--filter",
        action="append",
        default=[],
        metavar="COLUMN=VALUE",
        help=(
            "Filter rows before generating comparison views. "
            "May be supplied multiple times."
        ),
    )

    parser.add_argument(
        "--plots",
        action="store_true",
        help=(
            "Generate one plot per heuristic showing the result over num_nodes, "
            "with one line for each hack combination."
        ),
    )

    parser.add_argument(
        "--value-name",
        default="result",
        help=(
            "Name to use for the measured heuristic value "
            "(default: result; e.g. error_level)."
        ),
    )

    parser.add_argument(
        "--no-html",
        action="store_true",
        help="Do not generate the HTML report.",
    )

    return parser.parse_args()


def is_heuristic_column(column: str) -> bool:
    """
    Heuristic-result columns contain at least two '__' separators.
    """
    return column.count(HACK_SEPARATOR) >= 2


def parse_heuristic_name(name: str) -> dict[str, object]:
    """
    Break the heuristic name into useful dimensions.

    Examples:
        static_foresighted_expected_error
        greedy_nearsighted_incrementing_probability_success

    Unknown patterns are still preserved in 'heuristic'.
    """
    result = {
        "family": None,
        "foresight": None,
        "direction": None,
        "comparator": None,
    }

    if name in {"dynamic", "random"}:
        result["family"] = name
        return result

    parts = name.split("_")

    if len(parts) >= 3 and parts[0] == "static":
        result["family"] = "static"
        result["foresight"] = parts[1]
        result["comparator"] = "_".join(parts[2:])
        return result

    if len(parts) >= 4 and parts[0] == "greedy":
        result["family"] = "greedy"
        result["foresight"] = parts[1]
        result["direction"] = parts[2]
        result["comparator"] = "_".join(parts[3:])
        return result

    result["family"] = parts[0] if parts else name
    return result


def wide_to_long(df: pd.DataFrame, value_name: str) -> tuple[pd.DataFrame, list[str]]:
    """
    Convert heuristic columns from wide format to long format.
    """
    heuristic_columns = [
        column for column in df.columns
        if is_heuristic_column(column)
    ]

    if not heuristic_columns:
        raise ValueError(
            "No heuristic columns were found. Expected names like "
            "'dynamic__independent__variant'."
        )

    config_columns = [
        column for column in df.columns
        if column not in heuristic_columns
    ]

    long_df = df.melt(
        id_vars=config_columns,
        value_vars=heuristic_columns,
        var_name="_configuration",
        value_name=value_name,
    )

    parsed = long_df["_configuration"].str.rsplit(
        HACK_SEPARATOR,
        n=2,
        expand=True,
    )

    long_df["heuristic"] = parsed[0]
    long_df["temporal_redundancy"] = parsed[1]
    long_df["reactive_rescheduling"] = parsed[2]

    heuristic_parts = (
        long_df["heuristic"]
        .apply(parse_heuristic_name)
        .apply(pd.Series)
    )

    long_df = pd.concat([long_df, heuristic_parts], axis=1)
    long_df = long_df.drop(columns="_configuration")

    preferred_order = [
        *config_columns,
        "heuristic",
        "family",
        "foresight",
        "direction",
        "comparator",
        "temporal_redundancy",
        "reactive_rescheduling",
        value_name,
    ]

    long_df = long_df[preferred_order]

    return long_df, config_columns


def parse_filter_value(raw: str) -> object:
    """
    Convert CLI filter text into int/float/bool when possible.
    """
    lowered = raw.lower()

    if lowered == "true":
        return True
    if lowered == "false":
        return False
    if lowered in {"none", "null"}:
        return None

    try:
        return int(raw)
    except ValueError:
        pass

    try:
        return float(raw)
    except ValueError:
        pass

    return raw


def values_equal(series: pd.Series, target: object) -> pd.Series:
    """
    Numeric comparisons use a small tolerance; strings use exact matching.
    """
    if target is None:
        return series.isna()

    if isinstance(target, (int, float)) and not isinstance(target, bool):
        numeric = pd.to_numeric(series, errors="coerce")
        tolerance = 1e-9
        return (numeric - float(target)).abs() <= tolerance * (
                1.0 + abs(float(target))
        )

    return series.astype(str) == str(target)


def apply_filters(
        df: pd.DataFrame,
        filters: list[str],
) -> tuple[pd.DataFrame, list[tuple[str, object]]]:
    filtered = df.copy()
    parsed_filters: list[tuple[str, object]] = []

    for expression in filters:
        if "=" not in expression:
            raise ValueError(
                f"Invalid filter '{expression}'. Expected COLUMN=VALUE."
            )

        column, raw_value = expression.split("=", 1)
        column = column.strip()
        raw_value = raw_value.strip()

        if column not in filtered.columns:
            raise ValueError(
                f"Unknown filter column '{column}'. "
                f"Available columns include: {', '.join(filtered.columns)}"
            )

        value = parse_filter_value(raw_value)
        mask = values_equal(filtered[column], value)
        filtered = filtered.loc[mask].copy()
        parsed_filters.append((column, value))

    return filtered, parsed_filters


def summarize_repetitions(
        df: pd.DataFrame,
        config_columns: list[str],
        value_name: str,
) -> pd.DataFrame:
    """
    Average repetitions WITHOUT collapsing other experiment dimensions.

    repetition_id is the only original configuration column intentionally
    removed from the grouping.
    """
    group_columns = [
        column for column in config_columns
        if column != "repetition_id"
    ]

    group_columns += [
        "heuristic",
        "family",
        "foresight",
        "direction",
        "comparator",
        "temporal_redundancy",
        "reactive_rescheduling",
    ]

    summary = (
        df.groupby(group_columns, dropna=False)[value_name]
        .agg(
            mean="mean",
            std="std",
            minimum="min",
            maximum="max",
            repetitions="count",
        )
        .reset_index()
    )

    return summary


def make_node_comparison(
        df: pd.DataFrame,
        config_columns: list[str],
        value_name: str,
) -> pd.DataFrame:
    """
    Human-readable pivot table.

    Rows are experiment parameters + num_nodes + heuristic.
    Columns are temporal redundancy / reactive rescheduling combinations.

    repetition_id is excluded so repeated runs are averaged.
    """
    row_columns = [
        column for column in config_columns
        if column != "repetition_id"
    ]

    # Keep heuristic last so node/config groupings are easy to scan.
    row_columns.append("heuristic")

    comparison = pd.pivot_table(
        df,
        index=row_columns,
        columns=[
            "temporal_redundancy",
            "reactive_rescheduling",
        ],
        values=value_name,
        aggfunc="mean",
        dropna=False,
    )

    comparison = comparison.sort_index()

    # Flatten the MultiIndex columns for easy CSV viewing.
    comparison.columns = [
        f"temporal={temporal} | reactive={reactive}"
        for temporal, reactive in comparison.columns
    ]

    return comparison.reset_index()


def make_compact_node_comparison(
        df: pd.DataFrame,
        value_name: str,
) -> pd.DataFrame:
    """
    A compact comparison using only num_nodes + heuristic as rows.

    This is most meaningful when the user has filtered the input to one
    experiment configuration (e.g. a single e_fail_multiplier/deadline/etc.).
    """
    if "num_nodes" not in df.columns:
        raise ValueError("The CSV does not contain a 'num_nodes' column.")

    comparison = pd.pivot_table(
        df,
        index=["num_nodes", "heuristic"],
        columns=[
            "temporal_redundancy",
            "reactive_rescheduling",
        ],
        values=value_name,
        aggfunc="mean",
    )

    comparison.columns = [
        f"temporal={temporal} | reactive={reactive}"
        for temporal, reactive in comparison.columns
    ]

    return comparison.reset_index()


def varying_columns(
        df: pd.DataFrame,
        config_columns: list[str],
) -> dict[str, list[object]]:
    """
    Find experiment/configuration columns that still have multiple values.

    repetition_id is ignored because repeated runs are expected.
    """
    result: dict[str, list[object]] = {}

    for column in config_columns:
        if column == "repetition_id":
            continue

        values = df[column].drop_duplicates().tolist()

        if len(values) > 1:
            result[column] = values

    return result


def safe_filename(text: str) -> str:
    text = re.sub(r"[^A-Za-z0-9_.-]+", "_", text)
    return text.strip("_") or "plot"


def generate_plots(
        df: pd.DataFrame,
        output_dir: Path,
        value_name: str,
) -> None:
    """
    Generate one line plot per heuristic.

    Each line corresponds to one:
        temporal_redundancy + reactive_rescheduling
    combination.

    Values are averaged over repetitions and any remaining rows after filters.
    """
    import matplotlib.pyplot as plt

    if "num_nodes" not in df.columns:
        print("Skipping plots: no num_nodes column.")
        return

    plot_dir = output_dir / "plots"
    plot_dir.mkdir(parents=True, exist_ok=True)

    plot_data = (
        df.groupby(
            [
                "heuristic",
                "num_nodes",
                "temporal_redundancy",
                "reactive_rescheduling",
            ],
            dropna=False,
        )[value_name]
        .mean()
        .reset_index()
    )

    for heuristic, heuristic_df in plot_data.groupby("heuristic"):
        fig, ax = plt.subplots(figsize=(10, 6))

        for (temporal, reactive), hack_df in heuristic_df.groupby(
                ["temporal_redundancy", "reactive_rescheduling"],
                dropna=False,
        ):
            hack_df = hack_df.sort_values("num_nodes")

            label = f"{temporal} / {reactive}"

            ax.plot(
                hack_df["num_nodes"],
                hack_df[value_name],
                marker="o",
                label=label,
            )

        ax.set_title(str(heuristic))
        ax.set_xlabel("Number of nodes")
        ax.set_ylabel(value_name)
        ax.grid(True, alpha=0.25)
        ax.legend(title="Temporal / Reactive")
        fig.tight_layout()

        filename = plot_dir / f"{safe_filename(str(heuristic))}.png"
        fig.savefig(filename, dpi=160)
        plt.close(fig)


def dataframe_to_html_table(df: pd.DataFrame) -> str:
    return df.to_html(
        index=False,
        border=0,
        classes="dataframe",
        float_format=lambda x: f"{x:.6g}",
    )


def generate_html_report(
        df: pd.DataFrame,
        compact_comparison: pd.DataFrame,
        output_file: Path,
        value_name: str,
        parsed_filters: list[tuple[str, object]],
        varying: dict[str, list[object]],
) -> None:
    """
    Generate a self-contained HTML report with a separate comparison table
    for each node count.
    """
    filter_text = (
        ", ".join(f"{column}={value}" for column, value in parsed_filters)
        if parsed_filters
        else "None"
    )

    varying_html = ""

    if varying:
        rows = []
        for column, values in varying.items():
            preview = ", ".join(str(value) for value in values[:10])
            if len(values) > 10:
                preview += f", ... ({len(values)} values)"
            rows.append(
                f"<tr><td>{column}</td><td>{preview}</td></tr>"
            )

        varying_html = f"""
        <div class="warning">
            <strong>Parameters still varying after filtering.</strong>
            The compact node tables below average across these values.
            Add --filter options if you want to isolate one scenario.
            <table>
                <thead><tr><th>Parameter</th><th>Values</th></tr></thead>
                <tbody>{''.join(rows)}</tbody>
            </table>
        </div>
        """
    else:
        varying_html = """
        <div class="ok">
            All experiment parameters other than num_nodes/repetition_id
            are fixed after filtering.
        </div>
        """

    sections = []

    if "num_nodes" in compact_comparison.columns:
        for node_count in sorted(
                compact_comparison["num_nodes"].dropna().unique()
        ):
            node_df = compact_comparison[
                compact_comparison["num_nodes"] == node_count
                ].drop(columns=["num_nodes"])

            sections.append(
                f"""
                <h2>{node_count} nodes</h2>
                {dataframe_to_html_table(node_df)}
                """
            )

    unique_heuristics = df["heuristic"].nunique()
    row_count = len(df)

    html = f"""<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>Simulation Results Report</title>
<style>
    body {{
        font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI",
                     sans-serif;
        margin: 32px;
        color: #222;
    }}

    h1 {{
        margin-bottom: 4px;
    }}

    h2 {{
        margin-top: 36px;
        border-bottom: 1px solid #ccc;
        padding-bottom: 6px;
    }}

    .meta {{
        color: #555;
        margin-bottom: 24px;
    }}

    .warning {{
        background: #fff4d6;
        border: 1px solid #d8b24c;
        padding: 14px;
        margin: 18px 0;
    }}

    .ok {{
        background: #edf7ed;
        border: 1px solid #8bb58b;
        padding: 14px;
        margin: 18px 0;
    }}

    table {{
        border-collapse: collapse;
        width: 100%;
        margin-top: 10px;
        margin-bottom: 26px;
        font-size: 13px;
    }}

    th, td {{
        border: 1px solid #ddd;
        padding: 7px 9px;
        text-align: right;
        white-space: nowrap;
    }}

    th:first-child, td:first-child {{
        text-align: left;
    }}

    thead th {{
        position: sticky;
        top: 0;
        background: #f3f3f3;
    }}

    tbody tr:nth-child(even) {{
        background: #fafafa;
    }}
</style>
</head>
<body>

<h1>Simulation Results Report</h1>

<div class="meta">
    Rows after normalization/filtering: {row_count:,}<br>
    Heuristics: {unique_heuristics}<br>
    Metric: {value_name}<br>
    Filters: {filter_text}
</div>

{varying_html}

<p>
Each table groups rows by <strong>num_nodes</strong>.
Rows are heuristics. Columns are the combinations of
<strong>temporal redundancy</strong> and
<strong>reactive rescheduling</strong>.
Values are means over matching repetitions/rows.
</p>

{''.join(sections)}

</body>
</html>
"""

    output_file.write_text(html, encoding="utf-8")


def print_dataset_overview(
        df: pd.DataFrame,
        config_columns: list[str],
        value_name: str,
) -> None:
    print()
    print("=" * 80)
    print("DATASET OVERVIEW")
    print("=" * 80)

    print(f"Normalized rows: {len(df):,}")
    print(f"Heuristics:      {df['heuristic'].nunique()}")

    if "num_nodes" in df.columns:
        print(
            "Node counts:     "
            + ", ".join(
                map(
                    str,
                    sorted(df["num_nodes"].dropna().unique()),
                )
            )
        )

    print()
    print("Hack values:")

    print(
        "  Temporal redundancy: "
        + ", ".join(
            map(
                str,
                sorted(df["temporal_redundancy"].dropna().unique()),
            )
        )
    )

    print(
        "  Reactive rescheduling: "
        + ", ".join(
            map(
                str,
                sorted(df["reactive_rescheduling"].dropna().unique()),
            )
        )
    )

    print()
    print("Experiment parameters with more than one value:")

    any_varying = False

    for column in config_columns:
        if column == "repetition_id":
            continue

        count = df[column].nunique(dropna=False)

        if count > 1:
            any_varying = True
            values = df[column].drop_duplicates().tolist()
            preview = ", ".join(map(str, values[:8]))

            if len(values) > 8:
                preview += ", ..."

            print(f"  {column}: {preview}")

    if not any_varying:
        print("  None")

    print()
    print(f"Measured value column: {value_name}")
    print()


def main() -> None:
    args = parse_args()

    if not args.csv_file.exists():
        raise FileNotFoundError(args.csv_file)

    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Reading {args.csv_file} ...")
    raw_df = pd.read_csv(args.csv_file)

    long_df, config_columns = wide_to_long(
        raw_df,
        value_name=args.value_name,
    )

    print_dataset_overview(
        long_df,
        config_columns,
        args.value_name,
    )

    # Always save the complete normalized form before filters.
    long_file = args.output_dir / "results_long.csv"
    long_df.to_csv(long_file, index=False)

    filtered_df, parsed_filters = apply_filters(
        long_df,
        args.filter,
    )

    if filtered_df.empty:
        raise ValueError(
            "The selected filters produced zero rows."
        )

    print(f"Rows after filtering: {len(filtered_df):,}")

    # Detailed summary: repetitions are averaged but other experiment
    # parameters remain distinct.
    detailed_summary = summarize_repetitions(
        filtered_df,
        config_columns,
        args.value_name,
    )

    summary_file = args.output_dir / "results_summary.csv"
    detailed_summary.to_csv(summary_file, index=False)

    # Safe/detailed comparison retains every original config field except
    # repetition_id.
    node_comparison = make_node_comparison(
        filtered_df,
        config_columns,
        args.value_name,
    )

    comparison_file = args.output_dir / "node_comparison_detailed.csv"
    node_comparison.to_csv(comparison_file, index=False)

    # Compact version is the one most useful for visual inspection.
    compact_comparison = make_compact_node_comparison(
        filtered_df,
        args.value_name,
    )

    compact_file = args.output_dir / "node_comparison_compact.csv"
    compact_comparison.to_csv(compact_file, index=False)

    # Identify whether compact aggregation is mixing other experiment
    # parameters.
    remaining_varying = varying_columns(
        filtered_df,
        config_columns,
    )

    # num_nodes is intentionally allowed to vary.
    remaining_varying.pop("num_nodes", None)

    if remaining_varying:
        print()
        print("WARNING:")
        print(
            "The compact node comparison is averaging across additional "
            "experiment parameters:"
        )

        for column, values in remaining_varying.items():
            preview = ", ".join(map(str, values[:8]))
            if len(values) > 8:
                preview += ", ..."
            print(f"  {column}: {preview}")

        print()
        print(
            "Use --filter COLUMN=VALUE to isolate a scenario when you do "
            "not want those values mixed."
        )

    if not args.no_html:
        report_file = args.output_dir / "report.html"

        generate_html_report(
            filtered_df,
            compact_comparison,
            report_file,
            args.value_name,
            parsed_filters,
            remaining_varying,
        )

    if args.plots:
        generate_plots(
            filtered_df,
            args.output_dir,
            args.value_name,
        )

    print()
    print("=" * 80)
    print("GENERATED FILES")
    print("=" * 80)
    print(f"Long-format data:       {long_file}")
    print(f"Repetition summary:     {summary_file}")
    print(f"Detailed node table:    {comparison_file}")
    print(f"Compact node table:     {compact_file}")

    if not args.no_html:
        print(f"HTML report:            {args.output_dir / 'report.html'}")

    if args.plots:
        print(f"Plots:                  {args.output_dir / 'plots'}")

    print()
    print("Useful examples:")
    print(
        f"  python {Path(__file__).name} {args.csv_file} "
        "--filter e_fail_multiplier=2"
    )
    print(
        f"  python {Path(__file__).name} {args.csv_file} "
        "--filter e_fail_multiplier=2 --plots"
    )
    print(
        f"  python {Path(__file__).name} {args.csv_file} "
        "--filter family=greedy --filter comparator=expected_error"
    )


if __name__ == "__main__":
    main()