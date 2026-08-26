#!/usr/bin/env python3
"""Generate summary statistics and plots for one-host scheduling results."""

from __future__ import annotations

import argparse
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import TypedDict

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy.stats import t


# CSV naming conventions.
WITHOUT_TEMPORAL_REDUNDANCY_SUFFIX = "__off__off"
WITH_TEMPORAL_REDUNDANCY_SUFFIX = "__independent__off"
E_FAIL_MULTIPLIER_COLUMN = "e_fail_multiplier"

# Analysis choices.
REFERENCE_ALGORITHM = "dynamic"
BEST_HEURISTIC = "static_foresighted_expected_error"
DEFAULT_CONFIDENCE = 0.95
DEFAULT_BOOTSTRAP_RESAMPLES = 20_000
DEFAULT_RANDOM_SEED = 20260821

E_FAIL_GROUPS: dict[str, tuple[float, ...]] = {
    # "2.0": (2.0,),
    # "5.0": (5.0,),
    "any": (2.0, 5.0),
}

ALGORITHM_NAME_MAP : dict[str, str] = {
    "dynamic": "Dyn",
    "random": "Rand",
    "static_foresighted_expected_error": "FS-X",
    "static_foresighted_probability_success": "FS-P",
    "static_foresighted_success_error_ratio": "FS-R",
    "static_foresighted_error_level": "FS-E",
    "static_nearsighted_expected_error": "NS-X",
    "static_nearsighted_probability_success": "NS-P",
    "static_nearsighted_success_error_ratio": "NS-R",
    "static_nearsighted_error_level": "NS-E",
}


class WinTieLossStats(TypedDict):
    """Per-instance comparison statistics from the reference perspective."""

    wins: int
    ties: int
    losses: int
    mean_comparison_degradation_when_losing: float | None
    mean_comparison_improvement_when_winning: float | None


def algorithm_column(algorithm_name: str, suffix: str) -> str:
    """Return the CSV column name for an algorithm and execution mode."""
    return f"{algorithm_name}{suffix}"


def discover_algorithm_names(frame: pd.DataFrame) -> list[str]:
    """Discover algorithms that have both temporal-redundancy variants.

    Algorithms are returned in their CSV column order. This avoids relying on
    fixed metadata-column positions and gives deterministic plot ordering when
    means are equal.
    """
    columns = set(frame.columns)
    algorithm_names: list[str] = []

    for column in frame.columns:
        if not column.endswith(WITHOUT_TEMPORAL_REDUNDANCY_SUFFIX):
            continue

        algorithm_name = column[: -len(WITHOUT_TEMPORAL_REDUNDANCY_SUFFIX)]
        with_tr_column = algorithm_column(
            algorithm_name,
            WITH_TEMPORAL_REDUNDANCY_SUFFIX,
        )
        if with_tr_column in columns:
            algorithm_names.append(algorithm_name)

    if not algorithm_names:
        raise ValueError(
            "Could not find any algorithms with both '__off__off' and "
            "'__independent__off' columns."
        )

    return algorithm_names


def require_columns(frame: pd.DataFrame, columns: Sequence[str]) -> None:
    """Raise a clear error if any required CSV columns are absent."""
    missing = sorted(set(columns) - set(frame.columns))
    if missing:
        raise KeyError(f"Missing CSV column(s): {missing}")


def numeric_pairs(
    frame: pd.DataFrame,
    reference_column: str,
    comparison_column: str,
    *,
    row_mask: pd.Series | np.ndarray | None = None,
) -> tuple[np.ndarray, np.ndarray]:
    """Extract complete, finite paired values from two CSV columns."""
    require_columns(frame, [reference_column, comparison_column])

    selected = frame if row_mask is None else frame.loc[row_mask]
    pairs = selected[[reference_column, comparison_column]].apply(
        pd.to_numeric,
        errors="coerce",
    )
    finite_rows = np.isfinite(pairs.to_numpy()).all(axis=1)
    pairs = pairs.loc[finite_rows]

    if pairs.empty:
        raise ValueError(
            "The selected rows contain no complete finite pairs for "
            f"{reference_column!r} and {comparison_column!r}."
        )

    reference = pairs[reference_column].to_numpy(dtype=float)
    comparison = pairs[comparison_column].to_numpy(dtype=float)
    return reference, comparison


def collect_algorithm_values(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
    *,
    suffix: str,
    row_mask: pd.Series | np.ndarray | None = None,
) -> dict[str, np.ndarray]:
    """Collect one aligned array per algorithm for a selected set of rows.

    Invalid values are represented by NaN here and filtered later, either per
    algorithm or per algorithm pair. Keeping the arrays aligned preserves the
    paired experimental design.
    """
    columns = [algorithm_column(name, suffix) for name in algorithm_names]
    require_columns(frame, columns)

    selected = frame if row_mask is None else frame.loc[row_mask]
    if selected.empty:
        raise ValueError("The selected row subset is empty.")

    values_by_algorithm: dict[str, np.ndarray] = {}
    for algorithm_name, column in zip(algorithm_names, columns, strict=True):
        values_by_algorithm[algorithm_name] = pd.to_numeric(
            selected[column],
            errors="coerce",
        ).to_numpy(dtype=float)

    return values_by_algorithm


def count_wins_ties_losses(
    reference: np.ndarray,
    comparison: np.ndarray,
    *,
    rtol: float = 1e-9,
    atol: float = 1e-12,
) -> WinTieLossStats:
    """Summarize wins, ties, losses, and their conditional effect sizes.

    Lower values are better. Counts are from the reference algorithm's
    perspective. Relative gains and degradations are from the comparison
    algorithm's perspective and use the reference value as denominator.
    """
    reference = np.asarray(reference, dtype=float)
    comparison = np.asarray(comparison, dtype=float)

    if reference.shape != comparison.shape:
        raise ValueError("reference and comparison must have the same shape")
    if reference.size == 0:
        raise ValueError("reference and comparison must not be empty")
    if not np.isfinite(reference).all() or not np.isfinite(comparison).all():
        raise ValueError("reference and comparison must contain finite values")
    if np.any(reference <= 0.0):
        raise ValueError(
            "All reference values must be positive to compute relative "
            "gains and losses."
        )

    tolerance = atol + rtol * np.maximum(
        np.abs(reference),
        np.abs(comparison),
    )
    ties = np.abs(reference - comparison) <= tolerance
    reference_wins = (reference < comparison) & ~ties
    reference_losses = (reference > comparison) & ~ties

    relative_difference = (comparison - reference) / reference
    mean_comparison_degradation = (
        float(np.mean(relative_difference[reference_wins]))
        if np.any(reference_wins)
        else None
    )
    mean_comparison_improvement = (
        float(np.mean(-relative_difference[reference_losses]))
        if np.any(reference_losses)
        else None
    )

    return {
        "wins": int(np.count_nonzero(reference_wins)),
        "ties": int(np.count_nonzero(ties)),
        "losses": int(np.count_nonzero(reference_losses)),
        "mean_comparison_degradation_when_losing": (
            mean_comparison_degradation
        ),
        "mean_comparison_improvement_when_winning": (
            mean_comparison_improvement
        ),
    }


def mean_t_confidence_interval(
    values: np.ndarray,
    confidence: float,
) -> tuple[float, float, float]:
    """Return (sample mean, lower bound, upper bound) for a t interval."""
    values = np.asarray(values, dtype=float)
    values = values[np.isfinite(values)]

    if values.size < 2:
        raise ValueError("At least two finite observations are required.")
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be strictly between 0 and 1")

    estimate = float(np.mean(values))
    standard_error = float(np.std(values, ddof=1) / np.sqrt(values.size))
    alpha = 1.0 - confidence
    critical_value = float(
        t.ppf(1.0 - alpha / 2.0, df=values.size - 1)
    )
    margin = critical_value * standard_error
    return estimate, estimate - margin, estimate + margin


def paired_bootstrap_ratio_of_means(
    reference: np.ndarray,
    comparison: np.ndarray,
    confidence: float,
    resamples: int,
    seed: int,
    batch_size: int = 200,
) -> tuple[float, float, float]:
    """Percentile-bootstrap CI for E[comparison] / E[reference] - 1.

    Rows are resampled as pairs, preserving the paired experimental design.
    """
    reference = np.asarray(reference, dtype=float)
    comparison = np.asarray(comparison, dtype=float)

    if reference.shape != comparison.shape:
        raise ValueError("reference and comparison must have the same shape")
    if reference.size < 2:
        raise ValueError("At least two paired observations are required.")
    if not np.isfinite(reference).all() or not np.isfinite(comparison).all():
        raise ValueError("reference and comparison must contain finite values")
    if reference.mean() <= 0.0:
        raise ValueError("The mean reference value must be positive.")
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be strictly between 0 and 1")
    if resamples < 1:
        raise ValueError("The number of bootstrap resamples must be positive.")
    if batch_size < 1:
        raise ValueError("batch_size must be positive.")

    rng = np.random.default_rng(seed)
    bootstrap_estimates = np.empty(resamples, dtype=float)
    n = reference.size

    for start in range(0, resamples, batch_size):
        stop = min(start + batch_size, resamples)
        indices = rng.integers(0, n, size=(stop - start, n))
        reference_means = reference[indices].mean(axis=1)
        comparison_means = comparison[indices].mean(axis=1)
        bootstrap_estimates[start:stop] = (
            comparison_means / reference_means - 1.0
        )

    alpha = 1.0 - confidence
    low, high = np.quantile(
        bootstrap_estimates,
        [alpha / 2.0, 1.0 - alpha / 2.0],
    )
    estimate = float(comparison.mean() / reference.mean() - 1.0)
    return estimate, float(low), float(high)


def relative_improvement_between_means(
    baseline: np.ndarray,
    improved: np.ndarray,
) -> float:
    """Return (mean(baseline) - mean(improved)) / mean(baseline)."""
    baseline_mean = float(np.mean(baseline))
    improved_mean = float(np.mean(improved))

    if baseline_mean <= 0.0:
        raise ValueError("The baseline mean must be positive.")

    return (baseline_mean - improved_mean) / baseline_mean


def save_or_show_figure(fig: plt.Figure, output_file: Path | None) -> None:
    """Save a figure and close it, or display it when no path is supplied."""
    fig.tight_layout()

    if output_file is None:
        plt.show()
        return

    output_file.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output_file, dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"  Saved plot to {output_file}")


def plot_average_errors(
    values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
    *,
    confidence: float = DEFAULT_CONFIDENCE,
    output_file: Path | None = None,
) -> None:
    """Plot each algorithm's mean error and t confidence interval."""
    results: list[tuple[str, float, float, float]] = []

    for algorithm_name, values in values_by_algorithm.items():
        mean, low, high = mean_t_confidence_interval(
            np.asarray(values, dtype=float),
            confidence,
        )
        results.append((algorithm_name, mean, low, high))

    # Lower error is better, so display the smallest mean first.
    results.sort(key=lambda result: result[1])

    algorithm_names = [ALGORITHM_NAME_MAP[result[0]] for result in results]
    means = np.array([result[1] for result in results])
    lower_bounds = np.array([result[2] for result in results])
    upper_bounds = np.array([result[3] for result in results])
    error_bars = np.vstack(
        [means - lower_bounds, upper_bounds - means]
    )

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
    save_or_show_figure(fig, output_file)


def plot_relative_errors_vs_reference(
    values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
    *,
    reference_name: str = REFERENCE_ALGORITHM,
    confidence: float = DEFAULT_CONFIDENCE,
    bootstrap_resamples: int = DEFAULT_BOOTSTRAP_RESAMPLES,
    seed: int = DEFAULT_RANDOM_SEED,
    output_file: Path | None = None,
) -> None:
    """Plot relative mean error versus a reference algorithm.

    Positive values mean that an algorithm has a higher mean error than the
    reference and is therefore worse.
    """
    if reference_name not in values_by_algorithm:
        raise KeyError(
            f"Reference algorithm {reference_name!r} is not present."
        )

    reference_all = np.asarray(
        values_by_algorithm[reference_name],
        dtype=float,
    )
    results: list[tuple[str, float, float, float]] = []

    for algorithm_name, algorithm_values in values_by_algorithm.items():
        if algorithm_name == reference_name:
            continue

        comparison_all = np.asarray(algorithm_values, dtype=float)
        if reference_all.shape != comparison_all.shape:
            raise ValueError(
                f"{algorithm_name!r} and {reference_name!r} do not have "
                "the same number of values."
            )

        finite_rows = np.isfinite(reference_all) & np.isfinite(comparison_all)
        reference = reference_all[finite_rows]
        comparison = comparison_all[finite_rows]

        if reference.size < 2:
            raise ValueError(
                f"Not enough valid paired values for {algorithm_name!r}."
            )

        estimate, low, high = paired_bootstrap_ratio_of_means(
            reference,
            comparison,
            confidence=confidence,
            resamples=bootstrap_resamples,
            seed=seed,
        )
        results.append(
            (
                algorithm_name,
                100.0 * estimate,
                100.0 * low,
                100.0 * high,
            )
        )

    # Put the closest competitors first.
    results.sort(key=lambda result: result[1])

    algorithm_names = [result[0] for result in results]
    estimates = np.array([result[1] for result in results])
    lower_bounds = np.array([result[2] for result in results])
    upper_bounds = np.array([result[3] for result in results])
    error_bars = np.vstack(
        [estimates - lower_bounds, upper_bounds - estimates]
    )
    y_positions = np.arange(len(algorithm_names))

    figure_height = max(4.0, 0.55 * len(algorithm_names))
    fig, ax = plt.subplots(figsize=(10, figure_height))
    ax.errorbar(
        estimates,
        y_positions,
        xerr=error_bars,
        fmt="o",
        capsize=6,
        linewidth=3,
    )

    # Print text labels
    for x,y in zip(estimates, y_positions):
        ax.text(x-3, y-0.25,f"{x:.2f}%")

    ax.axvline(0.0, linewidth=1.0, linestyle="--")
    ax.set_yticks(y_positions)
    ax.set_yticklabels([ALGORITHM_NAME_MAP[algorithm_name] for algorithm_name in algorithm_names], fontsize=12)
    ax.invert_yaxis()
    ax.set_xlabel(
        f"Increase in mean error relative to {ALGORITHM_NAME_MAP[reference_name]} (%)", fontsize=14
    )
    ax.tick_params(axis='x', labelsize=14)
    ax.set_ylabel("Heuristic", fontsize=14)
    # ax.set_title(
    #     f"Relative mean error compared with {reference_name} "
    #     f"({100.0 * confidence:g}% paired-bootstrap intervals)"
    # )
    ax.grid(axis="x", alpha=0.3)

    # Adjust the limits for prettiness
    ax.set_ylim(ymax=ax.get_ylim()[1] - 0.5)
    ax.set_xlim(xmin=-2)

    save_or_show_figure(fig, output_file)


def report_temporal_redundancy_improvements(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
) -> None:
    """Print the mean-error improvement due to temporal redundancy."""
    print("\n** IMPROVEMENT DUE TO TEMPORAL REDUNDANCY **")
    improvements: dict[str, float] = {}

    for algorithm_name in algorithm_names:
        # The random policy is not meaningful for this design comparison.
        if algorithm_name == "random":
            continue

        without_tr_column = algorithm_column(
            algorithm_name,
            WITHOUT_TEMPORAL_REDUNDANCY_SUFFIX,
        )
        with_tr_column = algorithm_column(
            algorithm_name,
            WITH_TEMPORAL_REDUNDANCY_SUFFIX,
        )
        without_tr, with_tr = numeric_pairs(
            frame,
            without_tr_column,
            with_tr_column,
        )
        improvements[algorithm_name] = 100.0 * (
            relative_improvement_between_means(without_tr, with_tr)
        )

    for algorithm_name, improvement in sorted(
        improvements.items(),
        key=lambda item: item[1],
    ):
        print(f"  - {algorithm_name}: {improvement:.2f}%")

    if improvements:
        print(
            "  Average improvement across algorithms: "
            f"{np.mean(list(improvements.values())):.2f}%"
        )


def generate_average_error_plots(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
    *,
    output_dir: Path,
    confidence: float,
    bootstrap_resamples: int,
    seed: int,
) -> None:
    """Generate absolute- and relative-error plots by failure multiplier."""
    print("\n** AVERAGE ERRORS **")
    require_columns(frame, [E_FAIL_MULTIPLIER_COLUMN])

    for group_name, multipliers in E_FAIL_GROUPS.items():
        row_mask = frame[E_FAIL_MULTIPLIER_COLUMN].isin(multipliers)
        matching_rows = int(row_mask.sum())
        if matching_rows == 0:
            raise ValueError(
                f"No rows match {E_FAIL_MULTIPLIER_COLUMN} in "
                f"{list(multipliers)}."
            )

        print(
            f"  {E_FAIL_MULTIPLIER_COLUMN} in {list(multipliers)}: "
            f"{matching_rows} rows"
        )
        values_by_algorithm = collect_algorithm_values(
            frame,
            algorithm_names,
            suffix=WITH_TEMPORAL_REDUNDANCY_SUFFIX,
            row_mask=row_mask,
        )

        plot_average_errors(
            values_by_algorithm,
            confidence=confidence,
            output_file=(
                output_dir / f"average_error_plot_efail_{group_name}.pdf"
            ),
        )
        plot_relative_errors_vs_reference(
            values_by_algorithm,
            reference_name=REFERENCE_ALGORITHM,
            confidence=confidence,
            bootstrap_resamples=bootstrap_resamples,
            seed=seed,
            output_file=(
                output_dir
                / f"relative_error_vs_dynamic_efail_{group_name}.pdf"
            ),
        )


def print_pair_comparison(
    reference: np.ndarray,
    comparison: np.ndarray,
    *,
    reference_label: str,
    comparison_label: str,
    confidence: float,
    bootstrap_resamples: int,
    seed: int,
) -> None:
    """Print a paired ratio-of-means analysis and win/tie/loss summary."""
    estimate, low, high = paired_bootstrap_ratio_of_means(
        reference,
        comparison,
        confidence=confidence,
        resamples=bootstrap_resamples,
        seed=seed,
    )

    print("\n  Relative difference between the two means")
    print("    theta = E[comparison] / E[reference] - 1")
    print(f"    Estimate: {100.0 * estimate:.4f}%")
    print(
        f"    {100.0 * confidence:g}% paired-bootstrap interval: "
        f"[{100.0 * low:.4f}%, {100.0 * high:.4f}%]"
    )

    counts = count_wins_ties_losses(reference, comparison)
    degradation = counts["mean_comparison_degradation_when_losing"]
    improvement = counts["mean_comparison_improvement_when_winning"]

    print("\n  Per-instance comparison")
    print(
        f"    {reference_label} wins / {comparison_label} losses: "
        f"{counts['wins']:6d}"
    )
    if degradation is None:
        print("      Mean comparison degradation: n/a (no comparison losses)")
    else:
        print(
            "      Mean comparison degradation when losing: "
            f"{100.0 * degradation:.4f}%"
        )

    print(
        f"    {reference_label} losses / {comparison_label} wins: "
        f"{counts['losses']:6d}"
    )
    if improvement is None:
        print("      Mean comparison improvement: n/a (no comparison wins)")
    else:
        print(
            "      Mean comparison improvement when winning: "
            f"{100.0 * improvement:.4f}%"
        )

    print(f"    Ties: {counts['ties']:6d}")


def report_dynamic_vs_best_heuristic(
    frame: pd.DataFrame,
    *,
    confidence: float,
    bootstrap_resamples: int,
    seed: int,
) -> None:
    """Compare the dynamic policy with the selected best static heuristic."""
    print("\n** COMPARISON BETWEEN DYNAMIC AND BEST HEURISTIC **")

    reference_column = algorithm_column(
        REFERENCE_ALGORITHM,
        WITH_TEMPORAL_REDUNDANCY_SUFFIX,
    )
    comparison_column = algorithm_column(
        BEST_HEURISTIC,
        WITH_TEMPORAL_REDUNDANCY_SUFFIX,
    )
    reference, comparison = numeric_pairs(
        frame,
        reference_column,
        comparison_column,
    )

    print_pair_comparison(
        reference,
        comparison,
        reference_label="Dynamic",
        comparison_label="best heuristic",
        confidence=confidence,
        bootstrap_resamples=bootstrap_resamples,
        seed=seed,
    )


def report_split_half_stability(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
    *,
    repeats: int = 500,
    seed: int = DEFAULT_RANDOM_SEED,
    group_column: str | None = "app_config_id",
) -> None:
    """
    Assess whether the principal conclusions remain stable when using
    only half of the independent sampling units.

    When group_column is not None, all rows with the same group value
    are kept in the same half. Use group_column=None to split rows
    independently.
    """
    if repeats < 1:
        raise ValueError("repeats must be positive")

    names = list(algorithm_names)
    columns = [
        algorithm_column(
            algorithm_name,
            WITH_TEMPORAL_REDUNDANCY_SUFFIX,
        )
        for algorithm_name in names
    ]

    require_columns(frame, columns)

    if group_column is not None:
        require_columns(frame, [group_column])

    # Use exactly the same complete rows for every algorithm so that
    # rankings are based on identical sets of observations.
    numeric = frame[columns].apply(
        pd.to_numeric,
        errors="coerce",
    )
    finite = np.isfinite(numeric.to_numpy()).all(axis=1)
    values = numeric.loc[finite].to_numpy(dtype=float)

    if values.shape[0] < 4:
        raise ValueError(
            "At least four complete observations are required."
        )

    if group_column is None:
        groups = None
        units = np.arange(values.shape[0])
        unit_description = "rows"
    else:
        groups = frame.loc[
            finite,
            group_column,
        ].to_numpy()

        units = pd.unique(groups)
        unit_description = group_column

    if len(units) < 4:
        raise ValueError(
            "At least four independent sampling units are required."
        )

    names_array = np.asarray(names, dtype=object)

    reference_index = names.index(REFERENCE_ALGORITHM)
    heuristic_index = names.index(BEST_HEURISTIC)

    def get_row_indices(
        selected_units: np.ndarray,
    ) -> np.ndarray:
        if groups is None:
            return selected_units.astype(int)

        return np.flatnonzero(
            np.isin(groups, selected_units)
        )

    def summarize(
        row_indices: np.ndarray,
    ) -> tuple[tuple[str, ...], float]:
        means = values[row_indices].mean(axis=0)

        ranking = tuple(
            names_array[np.argsort(means)]
        )

        heuristic_degradation = 100.0 * (
            means[heuristic_index]
            / means[reference_index]
            - 1.0
        )

        return ranking, heuristic_degradation

    # Full-data results.
    all_rows = np.arange(values.shape[0])
    full_ranking, full_degradation = summarize(all_rows)

    rng = np.random.default_rng(seed)

    half_rankings: list[tuple[str, ...]] = []
    half_degradations: list[float] = []
    half_to_half_differences: list[float] = []

    both_halves_dynamic_first = 0
    both_halves_same_top_two = 0

    for _ in range(repeats):
        shuffled_units = rng.permutation(units)
        first_units, second_units = np.array_split(
            shuffled_units,
            2,
        )

        first_ranking, first_degradation = summarize(
            get_row_indices(first_units)
        )
        second_ranking, second_degradation = summarize(
            get_row_indices(second_units)
        )

        half_rankings.extend(
            [first_ranking, second_ranking]
        )
        half_degradations.extend(
            [first_degradation, second_degradation]
        )

        if (
            first_ranking[0] == REFERENCE_ALGORITHM
            and second_ranking[0] == REFERENCE_ALGORITHM
        ):
            both_halves_dynamic_first += 1

        if (
            first_ranking[:2] == full_ranking[:2]
            and second_ranking[:2] == full_ranking[:2]
        ):
            both_halves_same_top_two += 1

        half_to_half_differences.append(
            abs(first_degradation - second_degradation)
        )

    dynamic_first_rate = np.mean(
        [
            ranking[0] == REFERENCE_ALGORITHM
            for ranking in half_rankings
        ]
    )

    same_top_two_rate = np.mean(
        [
            ranking[:2] == full_ranking[:2]
            for ranking in half_rankings
        ]
    )

    same_complete_ranking_rate = np.mean(
        [
            ranking == full_ranking
            for ranking in half_rankings
        ]
    )

    half_degradations_array = np.asarray(
        half_degradations
    )
    degradation_low, degradation_median, degradation_high = (
        np.quantile(
            half_degradations_array,
            [0.05, 0.50, 0.95],
        )
    )

    print("\n** SPLIT-HALF STABILITY **")
    print(
        f"  Independent units: {len(units)} "
        f"({unit_description})"
    )
    print(f"  Random partitions: {repeats}")
    print(
        "  Full-data top two: "
        f"{full_ranking[0]}, {full_ranking[1]}"
    )
    print(
        "  Full-data best-heuristic degradation: "
        f"{full_degradation:.2f}%"
    )

    print(
        "  Dynamic ranked first in half-samples: "
        f"{100.0 * dynamic_first_rate:.1f}%"
    )
    print(
        "  Full-data top two reproduced in half-samples: "
        f"{100.0 * same_top_two_rate:.1f}%"
    )
    print(
        "  Complete ranking reproduced in half-samples: "
        f"{100.0 * same_complete_ranking_rate:.1f}%"
    )
    print(
        "  Both halves ranked dynamic first: "
        f"{100.0 * both_halves_dynamic_first / repeats:.1f}% "
        "of partitions"
    )
    print(
        "  Both halves reproduced the full-data top two: "
        f"{100.0 * both_halves_same_top_two / repeats:.1f}% "
        "of partitions"
    )
    print(
        "  Half-sample best-heuristic degradation: "
        f"median {degradation_median:.2f}%, "
        "central 90% empirical range "
        f"[{degradation_low:.2f}%, "
        f"{degradation_high:.2f}%]"
    )
    print(
        "  Median absolute difference between half estimates: "
        f"{np.median(half_to_half_differences):.2f} "
        "percentage points"
    )

def build_argument_parser() -> argparse.ArgumentParser:
    """Create the command-line parser."""
    parser = argparse.ArgumentParser(
        description=(
            "Generate statistics and plots for one-host scheduling results. "
            "Lower error values are assumed to be better."
        )
    )
    parser.add_argument("csv_file", type=Path, help="Input CSV file")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("."),
        help="Directory for generated plots (default: current directory)",
    )
    parser.add_argument(
        "--confidence",
        type=float,
        default=DEFAULT_CONFIDENCE,
        help=f"Confidence level (default: {DEFAULT_CONFIDENCE})",
    )
    parser.add_argument(
        "--bootstrap-resamples",
        type=int,
        default=DEFAULT_BOOTSTRAP_RESAMPLES,
        help=(
            "Number of paired-bootstrap resamples "
            f"(default: {DEFAULT_BOOTSTRAP_RESAMPLES})"
        ),
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=DEFAULT_RANDOM_SEED,
        help=f"Random seed (default: {DEFAULT_RANDOM_SEED})",
    )
    return parser


def main() -> None:
    args = build_argument_parser().parse_args()

    if not 0.0 < args.confidence < 1.0:
        raise ValueError("--confidence must be strictly between 0 and 1.")
    if args.bootstrap_resamples < 1:
        raise ValueError("--bootstrap-resamples must be positive.")

    frame = pd.read_csv(args.csv_file)
    algorithm_names = discover_algorithm_names(frame)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Loaded {len(frame)} rows from {args.csv_file}")
    print(f"Discovered {len(algorithm_names)} algorithms")

    report_temporal_redundancy_improvements(frame, algorithm_names)
    generate_average_error_plots(
        frame,
        algorithm_names,
        output_dir=args.output_dir,
        confidence=args.confidence,
        bootstrap_resamples=args.bootstrap_resamples,
        seed=args.seed,
    )
    report_dynamic_vs_best_heuristic(
        frame,
        confidence=args.confidence,
        bootstrap_resamples=args.bootstrap_resamples,
        seed=args.seed,
    )
    report_split_half_stability(
        frame,
        algorithm_names,
        repeats=500,
        seed=args.seed,
        group_column="app_config_id",
    )


if __name__ == "__main__":
    main()
