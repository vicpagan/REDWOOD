#!/usr/bin/env python3
"""
Plot multi-PE scheduler degradation data from JSON, using a fixed
algorithm order across N-groups and a colorblind-friendly palette.

Expected JSON shape:
{
  "2":  {"algorithm_name": 0.0, "other_algorithm": 0.16, ...},
  "4":  {...},
  "8":  {...},
  "16": {...}
}

By default, values are interpreted as fractional degradations:
0.16 -> 16%.
"""

from __future__ import annotations

import argparse
import json
import math
from collections.abc import Mapping
from pathlib import Path
from typing import Any, Literal

import matplotlib.pyplot as plt
from matplotlib.axes import Axes
from matplotlib.figure import Figure


DEFAULT_LABELS: dict[str, str] = {
    "dynamic__dependent__aggressive": "DYN / DEP / AGG",
    "greedy_foresighted_expected_error__dependent__variant":
        "G-FS-X / DEP / VAR",
    "greedy_foresighted_success_error_ratio__dependent__aggressive":
        "G-FS-R / DEP / AGG",
    "greedy_nearsighted_success_error_ratio__dependent__aggressive":
        "G-NS-R / DEP / AGG",
    "greedy_foresighted_expected_error__dependent__off":
        "G-FS-X / DEP / OFF",
}

# 15-color, colorblind-friendly palette.
DEFAULT_COLORBLIND_PALETTE: tuple[str, ...] = (
    "#0072B2",  # blue
    "#E69F00",  # orange
    "#009E73",  # bluish green
    "#D55E00",  # vermillion
    "#CC79A7",  # reddish purple
    "#56B4E9",  # sky blue
    "#F0E442",  # yellow
    "#000000",  # black
    "#332288",  # dark blue
    "#88CCEE",  # light blue
    "#44AA99",  # teal
    "#117733",  # green
    "#999933",  # olive
    "#DDCC77",  # sand
    "#AA4499",  # magenta
)


def _load_ranking_data(
    source: str | Path | Mapping[str | int, Mapping[str, Any]],
) -> dict[int, dict[str, float]]:
    """Load and validate ranking data from a JSON path or mapping."""
    if isinstance(source, (str, Path)):
        path = Path(source)
        try:
            with path.open("r", encoding="utf-8") as handle:
                raw = json.load(handle)
        except OSError as exc:
            raise OSError(f"Could not read JSON file {path!s}: {exc}") from exc
        except json.JSONDecodeError as exc:
            raise ValueError(f"Invalid JSON in {path!s}: {exc}") from exc
    elif isinstance(source, Mapping):
        raw = source
    else:
        raise TypeError("source must be a JSON path or a mapping")

    if not isinstance(raw, Mapping) or not raw:
        raise ValueError("The JSON root must be a non-empty object")

    parsed: dict[int, dict[str, float]] = {}
    for n_key, algorithms in raw.items():
        try:
            n = int(n_key)
        except (TypeError, ValueError) as exc:
            raise ValueError(f"Invalid processor-count key: {n_key!r}") from exc
        if n <= 0:
            raise ValueError(f"Processor count must be positive; got {n}")

        if not isinstance(algorithms, Mapping) or not algorithms:
            raise ValueError(f"Entry for N={n} must be a non-empty object")

        parsed[n] = {}
        for algorithm, value in algorithms.items():
            try:
                numeric_value = float(value)
            except (TypeError, ValueError) as exc:
                raise ValueError(
                    f"Non-numeric value for {algorithm!r} at N={n}: {value!r}"
                ) from exc
            if not math.isfinite(numeric_value) or numeric_value < 0:
                raise ValueError(
                    f"Value for {algorithm!r} at N={n} must be finite "
                    f"and nonnegative; got {numeric_value}"
                )
            parsed[n][str(algorithm)] = numeric_value

    return dict(sorted(parsed.items()))


def _default_short_label(name: str) -> str:
    """Create a compact fallback label for an algorithm name."""
    if name in DEFAULT_LABELS:
        return DEFAULT_LABELS[name]

    parts = name.split("__")
    base_map = {
        "dynamic": "DYN",
        "random": "RAND",
        "greedy_foresighted_expected_error": "G-FS-X",
        "greedy_nearsighted_expected_error": "G-NS-X",
        "greedy_foresighted_probability_success": "G-FS-P",
        "greedy_nearsighted_probability_success": "G-NS-P",
        "greedy_foresighted_success_error_ratio": "G-FS-R",
        "greedy_nearsighted_success_error_ratio": "G-NS-R",
        "greedy_foresighted_error_level": "G-FS-E",
        "greedy_nearsighted_error_level": "G-NS-E",
    }
    option_map = {
        "independent": "IND",
        "dependent": "DEP",
        "aggressive": "AGG",
        "variant": "VAR",
        "off": "OFF",
    }

    base = base_map.get(parts[0], parts[0].replace("_", "-"))
    suffix = [option_map.get(part, part.replace("_", "-")) for part in parts[1:]]
    return " / ".join([base, *suffix])


def plot_multi_host_ranking(
    source: str | Path | Mapping[str | int, Mapping[str, Any]],
    output_path: str | Path | None = None,
    *,
    label_map: Mapping[str, str] | None = None,
    palette: list[str] | tuple[str, ...] | None = None,
    values_are_fractions: bool = True,
    sort_within_group: bool = False,
    algorithm_order: list[str] | tuple[str, ...] | None = None,
    figure_width: float = 3.45,
    row_height: float = 0.285,
    bar_height: float = 0.68,
    vertical_scale: float = 1.0,
    header_gap: float = 0.55,
    row_gap: float = 0.92,
    group_gap: float = 0.22,
    y_margin: float = 0.015,
    xscale: Literal["linear", "symlog"] = "linear",
    symlog_linthresh: float = 1.0,
    value_decimals: int = 1,
    dpi: int = 300,
) -> tuple[Figure, Axes]:
    """
    Create a tall, one-column horizontal bar chart grouped by processor count.
    """
    data = _load_ranking_data(source)
    labels = dict(DEFAULT_LABELS)
    if label_map:
        labels.update(label_map)

    if palette is None:
        palette = list(DEFAULT_COLORBLIND_PALETTE)
    else:
        palette = list(palette)
    if not palette:
        raise ValueError("palette must contain at least one color")

    scale = 100.0 if values_are_fractions else 1.0

    if bar_height <= 0:
        raise ValueError("bar_height must be positive")
    if vertical_scale <= 0:
        raise ValueError("vertical_scale must be positive")
    if header_gap <= 0 or row_gap <= 0 or group_gap < 0:
        raise ValueError(
            "header_gap and row_gap must be positive; group_gap must be nonnegative"
        )
    if y_margin < 0:
        raise ValueError("y_margin must be nonnegative")

    # Fixed global order by default so the eye can scan consistently.
    all_algorithms = list(
        dict.fromkeys(
            algorithm
            for _, algorithms in data.items()
            for algorithm in algorithms.keys()
        )
    )
    if algorithm_order is None:
        global_order = all_algorithms
    else:
        seen = set()
        global_order = []
        for algorithm in algorithm_order:
            if algorithm in all_algorithms and algorithm not in seen:
                global_order.append(algorithm)
                seen.add(algorithm)
        for algorithm in all_algorithms:
            if algorithm not in seen:
                global_order.append(algorithm)
                seen.add(algorithm)

    color_map = {
        algorithm: palette[index % len(palette)]
        for index, algorithm in enumerate(global_order)
    }

    # Build rows.
    records: list[tuple[float, int, str, float]] = []
    tick_positions: list[float] = []
    tick_labels: list[str] = []
    header_tick_indices: set[int] = set()

    y = 0.0
    for group_index, (n, algorithms) in enumerate(data.items()):
        header_tick_indices.add(len(tick_positions))
        tick_positions.append(y)
        tick_labels.append(f"N = {n}")
        y += header_gap * vertical_scale

        if sort_within_group:
            group_items = list(algorithms.items())
            group_items.sort(key=lambda item: (item[1], item[0]))
            ordered_algorithms = [name for name, _ in group_items]
        else:
            ordered_algorithms = [name for name in global_order if name in algorithms]

        for algorithm in ordered_algorithms:
            value = algorithms[algorithm] * scale
            records.append((y, n, algorithm, value))
            tick_positions.append(y)
            tick_labels.append(labels.get(algorithm, _default_short_label(algorithm)))
            y += row_gap * vertical_scale

        if group_index != len(data) - 1:
            y += group_gap * vertical_scale

    figure_height = max(
        2.2,
        row_height * vertical_scale * len(tick_positions) + 0.35,
    )

    with plt.rc_context(
        {
            "font.size": 7.0,
            "axes.labelsize": 7.5,
            "xtick.labelsize": 6.5,
            "ytick.labelsize": 6.5,
            "legend.fontsize": 6.5,
        }
    ):
        fig, ax = plt.subplots(
            figsize=(figure_width, figure_height),
            constrained_layout=True,
        )

        for algorithm in global_order:
            algorithm_rows = [
                (row_y, value)
                for row_y, _, row_algorithm, value in records
                if row_algorithm == algorithm
            ]
            if not algorithm_rows:
                continue
            ax.barh(
                [row_y for row_y, _ in algorithm_rows],
                [value for _, value in algorithm_rows],
                height=bar_height * vertical_scale,
                color=color_map.get(algorithm),
                edgecolor="black",
                linewidth=0.35,
            )

        max_value = max((value for _, _, _, value in records), default=0.0)
        for row_y, _, _, value in records:
            text = (
                "best"
                if math.isclose(value, 0.0, abs_tol=1e-12)
                else f"{value:.{value_decimals}f}%"
            )
            ax.annotate(
                text,
                xy=(value, row_y),
                xytext=(3, 0),
                textcoords="offset points",
                ha="left",
                va="center",
                fontsize=6.2,
                clip_on=False,
            )

        ax.set_yticks(tick_positions, tick_labels)
        ax.tick_params(axis="y", length=0)

        for index, tick_label in enumerate(ax.get_yticklabels()):
            if index in header_tick_indices:
                tick_label.set_fontweight("bold")

        ax.invert_yaxis()
        if tick_positions:
            ymin = min(tick_positions) - y_margin
            ymax = max(tick_positions) + y_margin
            ax.set_ylim(ymax, ymin)
        ax.margins(y=0)

        ax.set_xlabel("Degradation from best mean error (%)")
        ax.grid(axis="x", linestyle=":", linewidth=0.55)
        ax.set_axisbelow(True)
        ax.spines["top"].set_visible(False)
        ax.spines["right"].set_visible(False)

        if xscale == "symlog":
            ax.set_xscale("symlog", linthresh=symlog_linthresh)
        elif xscale != "linear":
            raise ValueError("xscale must be either 'linear' or 'symlog'")

        if max_value > 0:
            ax.set_xlim(0, max_value * 1.17)
        else:
            ax.set_xlim(0, 1)

        if output_path is not None:
            output = Path(output_path)
            output.parent.mkdir(parents=True, exist_ok=True)
            fig.savefig(output, dpi=dpi, bbox_inches="tight")

    return fig, ax


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Plot multi-PE scheduler degradation rankings."
    )
    parser.add_argument("json_file", type=Path, help="Input JSON file")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("multi_host_ranking.pdf"),
        help="Output plot path (default: multi_host_ranking.pdf)",
    )
    parser.add_argument(
        "--already-percent",
        action="store_true",
        help="Treat JSON values as percentages instead of fractions",
    )
    parser.add_argument(
        "--symlog",
        action="store_true",
        help="Use a symmetric-log x-axis to reveal small degradations",
    )
    parser.add_argument(
        "--bar-height",
        type=float,
        default=0.68,
        help="Base horizontal-bar thickness (default: 0.68)",
    )
    parser.add_argument(
        "--vertical-scale",
        type=float,
        default=1.0,
        help=(
            "Scale all vertical dimensions; e.g. 0.8 makes the figure "
            "about 20%% shorter (default: 1.0)"
        ),
    )
    parser.add_argument(
        "--header-gap",
        type=float,
        default=0.55,
        help="Gap from each N-label row to the first bar in that group (default: 0.55)",
    )
    parser.add_argument(
        "--row-gap",
        type=float,
        default=0.92,
        help="Gap between neighboring algorithm rows (default: 0.92)",
    )
    parser.add_argument(
        "--group-gap",
        type=float,
        default=0.22,
        help="Extra gap between N-groups (default: 0.22)",
    )
    parser.add_argument(
        "--y-margin",
        type=float,
        default=0.015,
        help="Fractional vertical outer margin (default: 0.015)",
    )
    args = parser.parse_args()

    plot_multi_host_ranking(
        args.json_file,
        output_path=args.output,
        values_are_fractions=not args.already_percent,
        bar_height=args.bar_height,
        vertical_scale=args.vertical_scale,
        header_gap=args.header_gap,
        row_gap=args.row_gap,
        group_gap=args.group_gap,
        y_margin=args.y_margin,
        xscale="symlog" if args.symlog else "linear",
    )


if __name__ == "__main__":
    main()
