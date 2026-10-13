#!/usr/bin/env python3
"""Plot mean errors and t confidence intervals from a JSON results file.

Input is the corresponding JSON produced by single_host_results_generation.py.
Only Matplotlib and NumPy are required; no statistics are recomputed here.
"""

from __future__ import annotations

import argparse
import json
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import matplotlib.pyplot as plt
import numpy as np


def load_plot_data(
    input_file: Path,
    group_name: str | None,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Read this plot's JSON and select a group without silently discarding others."""
    with input_file.open(encoding="utf-8") as stream:
        data = json.load(stream)
    if not isinstance(data, dict):
        raise ValueError("The input JSON must contain an object.")
    if data.get("schema_version") != 1:
        raise ValueError("Unsupported or missing JSON schema_version (expected 1).")
    if data.get("plot_type") != "average_errors":
        raise ValueError("Expected plot_type 'average_errors' in the input JSON.")

    groups = data.get("groups")
    if not isinstance(groups, dict) or not groups:
        raise ValueError("The input JSON must contain a nonempty 'groups' object.")
    available = ", ".join(groups)
    if group_name is None:
        if len(groups) != 1:
            raise ValueError(
                f"This JSON contains multiple groups; choose --group from: {available}"
            )
        group_name = next(iter(groups))
    if group_name not in groups:
        raise ValueError(f"Unknown group {group_name!r}. Available groups: {available}")

    group = groups[group_name]
    if not isinstance(group, dict):
        raise ValueError(f"Group {group_name!r} must contain an object.")
    results = group.get("results")
    if not isinstance(results, list) or not results:
        raise ValueError(f"Group {group_name!r} must contain a nonempty 'results' list.")
    if not all(isinstance(result, dict) for result in results):
        raise ValueError("Every result must be an object.")
    return data, group


def save_figure(fig: plt.Figure, output_file: Path) -> None:
    """Save a PDF using the original layout, then close the figure."""
    fig.tight_layout()
    output_file.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_file, format="pdf", dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved plot to {output_file}")


def plot_average_errors(
    results: Sequence[dict[str, Any]],
    *,
    confidence: float,
    output_file: Path,
) -> None:
    """Render the stored mean errors and confidence intervals without recomputing."""
    if not 0.0 < confidence < 1.0:
        raise ValueError("The JSON confidence level must be strictly between 0 and 1.")
    if not results:
        raise ValueError("No results to plot.")
    # Stable sorting also preserves the original order for tied mean errors.
    results = sorted(results, key=lambda result: result["mean"])
    algorithm_names = [result["label"] for result in results]
    means = np.array([result["mean"] for result in results], dtype=float)
    lower_bounds = np.array([result["ci_lower"] for result in results], dtype=float)
    upper_bounds = np.array([result["ci_upper"] for result in results], dtype=float)
    if not np.isfinite(np.vstack([means, lower_bounds, upper_bounds])).all():
        raise ValueError("Mean errors and confidence bounds must be finite.")
    error_bars = np.vstack([means - lower_bounds, upper_bounds - means])
    if np.any(error_bars < 0):
        raise ValueError("Each mean must lie within its confidence interval.")

    figure_height = max(4.0, 0.55 * len(algorithm_names))
    fig, ax = plt.subplots(figsize=(10, figure_height))
    ax.barh(
        algorithm_names,
        means,
        xerr=error_bars,
        capsize=4,
    )
    ax.invert_yaxis()
    ax.set_xlabel("Mean error")
    ax.set_ylabel("Algorithm")
    ax.set_title(
        "Mean error by algorithm "
        f"({100.0 * confidence:g}% confidence intervals)"
    )
    ax.grid(axis="x", alpha=0.3)

    for y_position, (mean, low, high) in enumerate(
        zip(means, lower_bounds, upper_bounds, strict=True)
    ):
        ax.text(
            high,
            y_position,
            f"  {mean:.2f} [{low:.2f}, {high:.2f}]",
            va="center",
            ha="left",
        )

    ax.margins(x=0.20)
    save_figure(fig, output_file)


def build_argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Plot mean errors and t confidence intervals from a JSON results file.",
        allow_abbrev=False,
    )
    parser.add_argument(
        "--json-result-file", required=True, type=Path, metavar="PATH",
        help="Input average-errors JSON file from single_host_results_generation.py.",
    )
    parser.add_argument(
        "--output-pdf", required=True, type=Path, metavar="PATH",
        help="Output PDF path; parent directories are created as needed.",
    )
    parser.add_argument(
        "--group", metavar="NAME",
        help="Failure-multiplier group to plot (automatic when there is only one).",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> None:
    parser = build_argument_parser()
    args = parser.parse_args(argv)
    if args.json_result_file.resolve() == args.output_pdf.resolve():
        parser.error("The input JSON and output PDF paths must be distinct.")
    try:
        data, group = load_plot_data(args.json_result_file, args.group)
        plot_average_errors(
            group["results"],
            confidence=float(data["confidence"]),
            output_file=args.output_pdf,
        )
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
