#!/usr/bin/env python3
"""Generate summary statistics and plots for multi-host scheduling results."""

from __future__ import annotations

import argparse
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import TypedDict

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from ipinfo.data import continents
from scipy.stats import t
import sys


# CSV naming conventions.
E_FAIL_MULTIPLIER_COLUMN = "e_fail_multiplier"

# Analysis choices.
REFERENCE_ALGORITHM = "dynamic"
BEST_HEURISTIC = "static_foresighted_expected_error"
DEFAULT_CONFIDENCE = 0.95
DEFAULT_BOOTSTRAP_RESAMPLES = 20_000
DEFAULT_RANDOM_SEED = 20260821
DEFAULT_EFAIL_MULTIPLIER = 0.0

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
        if not column.endswith("__off") and not column.endswith("__aggressive") and not column.endswith("__variant"):
            continue
        algorithm_names.append(column)
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
) -> (float, float, float):
    """Return (mean(baseline) - mean(improved)) / mean(baseline)."""
    baseline_mean = float(np.mean(baseline))
    improved_mean = float(np.mean(improved))

    if baseline_mean <= 0.0:
        raise ValueError("The baseline mean must be positive.")

    improvement = (baseline_mean - improved_mean) / baseline_mean

    return improvement, baseline_mean, improved_mean


# def save_or_show_figure(fig: plt.Figure, output_file: Path | None) -> None:
#     """Save a figure and close it, or display it when no path is supplied."""
#     fig.tight_layout()
#
#     if output_file is None:
#         plt.show()
#         return
#
#     output_file.parent.mkdir(parents=True, exist_ok=True)
#     fig.savefig(output_file, dpi=300, bbox_inches="tight")
#     plt.close(fig)
#     print(f"  Saved plot to {output_file}")
#
#
# def plot_average_errors(
#     values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
#     *,
#     confidence: float = DEFAULT_CONFIDENCE,
#     output_file: Path | None = None,
# ) -> None:
#     """Plot each algorithm's mean error and t confidence interval."""
#     results: list[tuple[str, float, float, float]] = []
#
#     for algorithm_name, values in values_by_algorithm.items():
#         mean, low, high = mean_t_confidence_interval(
#             np.asarray(values, dtype=float),
#             confidence,
#         )
#         results.append((algorithm_name, mean, low, high))
#
#     # Lower error is better, so display the smallest mean first.
#     results.sort(key=lambda result: result[1])
#
#     algorithm_names = [ALGORITHM_NAME_MAP[result[0]] for result in results]
#     means = np.array([result[1] for result in results])
#     lower_bounds = np.array([result[2] for result in results])
#     upper_bounds = np.array([result[3] for result in results])
#     error_bars = np.vstack(
#         [means - lower_bounds, upper_bounds - means]
#     )
#
#     figure_height = max(4.0, 0.55 * len(algorithm_names))
#     fig, ax = plt.subplots(figsize=(10, figure_height))
#     ax.barh(
#         algorithm_names,
#         means,
#         xerr=error_bars,
#         capsize=4,
#     )
#     ax.invert_yaxis()
#     ax.set_xlabel("Mean error")
#     ax.set_ylabel("Algorithm")
#     ax.set_title(
#         "Mean error by algorithm "
#         f"({100.0 * confidence:g}% confidence intervals)"
#     )
#     ax.grid(axis="x", alpha=0.3)
#
#     for y_position, (mean, low, high) in enumerate(
#         zip(means, lower_bounds, upper_bounds, strict=True)
#     ):
#         ax.text(
#             high,
#             y_position,
#             f"  {mean:.2f} [{low:.2f}, {high:.2f}]",
#             va="center",
#             ha="left",
#         )
#
#     ax.margins(x=0.20)
#     save_or_show_figure(fig, output_file)
#
#
# def plot_relative_errors_vs_reference(
#     values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
#     *,
#     reference_name: str = REFERENCE_ALGORITHM,
#     confidence: float = DEFAULT_CONFIDENCE,
#     bootstrap_resamples: int = DEFAULT_BOOTSTRAP_RESAMPLES,
#     seed: int = DEFAULT_RANDOM_SEED,
#     output_file: Path | None = None,
# ) -> None:
#     """Plot relative mean error versus a reference algorithm.
#
#     Positive values mean that an algorithm has a higher mean error than the
#     reference and is therefore worse.
#     """
#     if reference_name not in values_by_algorithm:
#         raise KeyError(
#             f"Reference algorithm {reference_name!r} is not present."
#         )
#
#     reference_all = np.asarray(
#         values_by_algorithm[reference_name],
#         dtype=float,
#     )
#     results: list[tuple[str, float, float, float]] = []
#
#     for algorithm_name, algorithm_values in values_by_algorithm.items():
#         if algorithm_name == reference_name:
#             continue
#
#         comparison_all = np.asarray(algorithm_values, dtype=float)
#         if reference_all.shape != comparison_all.shape:
#             raise ValueError(
#                 f"{algorithm_name!r} and {reference_name!r} do not have "
#                 "the same number of values."
#             )
#
#         finite_rows = np.isfinite(reference_all) & np.isfinite(comparison_all)
#         reference = reference_all[finite_rows]
#         comparison = comparison_all[finite_rows]
#
#         if reference.size < 2:
#             raise ValueError(
#                 f"Not enough valid paired values for {algorithm_name!r}."
#             )
#
#         estimate, low, high = paired_bootstrap_ratio_of_means(
#             reference,
#             comparison,
#             confidence=confidence,
#             resamples=bootstrap_resamples,
#             seed=seed,
#         )
#         results.append(
#             (
#                 algorithm_name,
#                 100.0 * estimate,
#                 100.0 * low,
#                 100.0 * high,
#             )
#         )
#
#     # Put the closest competitors first.
#     results.sort(key=lambda result: result[1])
#
#     algorithm_names = [result[0] for result in results]
#     estimates = np.array([result[1] for result in results])
#     lower_bounds = np.array([result[2] for result in results])
#     upper_bounds = np.array([result[3] for result in results])
#     error_bars = np.vstack(
#         [estimates - lower_bounds, upper_bounds - estimates]
#     )
#     y_positions = np.arange(len(algorithm_names))
#
#     figure_height = max(4.0, 0.55 * len(algorithm_names))
#     fig, ax = plt.subplots(figsize=(10, figure_height))
#     ax.errorbar(
#         estimates,
#         y_positions,
#         xerr=error_bars,
#         fmt="o",
#         capsize=6,
#         linewidth=3,
#     )
#
#     # Print text labels
#     for x,y in zip(estimates, y_positions):
#         ax.text(x-3, y-0.25,f"{x:.2f}%", fontsize=16)
#
#     ax.axvline(0.0, linewidth=1.0, linestyle="--")
#     ax.set_yticks(y_positions)
#     ax.set_yticklabels([ALGORITHM_NAME_MAP[algorithm_name] for algorithm_name in algorithm_names], fontsize=18)
#     ax.invert_yaxis()
#     ax.set_xlabel(
#         f"Increase in mean error relative to {ALGORITHM_NAME_MAP[reference_name]} (%)", fontsize=18
#     )
#     ax.tick_params(axis='x', labelsize=16)
#     ax.set_ylabel("Heuristic", fontsize=18)
#     # ax.set_title(
#     #     f"Relative mean error compared with {reference_name} "
#     #     f"({100.0 * confidence:g}% paired-bootstrap intervals)"
#     # )
#     ax.grid(axis="x", alpha=0.3)
#
#     # Adjust the limits for prettiness
#     ax.set_ylim(ymax=ax.get_ylim()[1] - 0.5)
#     ax.set_xlim(xmin=-2)
#
#     save_or_show_figure(fig, output_file)
#
#
# def report_temporal_redundancy_improvements(
#     frame: pd.DataFrame,
#     algorithm_names: Sequence[str],
# ) -> None:
#     """Print the mean-error improvement due to temporal redundancy."""
#     print("\n** IMPROVEMENT DUE TO TEMPORAL REDUNDANCY **")
#     improvements: dict[str, (float, float, float)] = {}
#
#     for algorithm_name in algorithm_names:
#         # The random policy is not meaningful for this design comparison.
#         if algorithm_name == "random":
#             continue
#
#         without_tr_column = algorithm_column(
#             algorithm_name,
#             WITHOUT_TEMPORAL_REDUNDANCY_SUFFIX,
#         )
#         with_tr_column = algorithm_column(
#             algorithm_name,
#             WITH_TEMPORAL_REDUNDANCY_SUFFIX,
#         )
#         without_tr, with_tr = numeric_pairs(
#             frame,
#             without_tr_column,
#             with_tr_column,
#         )
#         improvement, without_tr, with_tr = relative_improvement_between_means(without_tr, with_tr)
#         improvements[algorithm_name] = (100.0 * improvement, without_tr, with_tr)
#
#     for algorithm_name, (improvement, without_tr, with_tr) in sorted(
#         improvements.items(),
#         key=lambda item: item[1],
#     ):
#         print(f"  - {algorithm_name}: {improvement:.2f}%  (without tr: {without_tr:.2f}, with tr: {with_tr:.2f})")
#
#     if improvements:
#         print(
#             "  Average improvement across algorithms: "
#             f"{np.mean(list([x[0] for x in improvements.values()])):.2f}%"
#         )
#
#
# def generate_average_error_plots(
#     frame: pd.DataFrame,
#     algorithm_names: Sequence[str],
#     *,
#     output_dir: Path,
#     confidence: float,
#     bootstrap_resamples: int,
#     seed: int,
# ) -> None:
#     """Generate absolute- and relative-error plots by failure multiplier."""
#     print("\n** AVERAGE ERRORS **")
#     require_columns(frame, [E_FAIL_MULTIPLIER_COLUMN])
#
#     for group_name, multipliers in E_FAIL_GROUPS.items():
#         row_mask = frame[E_FAIL_MULTIPLIER_COLUMN].isin(multipliers)
#         matching_rows = int(row_mask.sum())
#         if matching_rows == 0:
#             raise ValueError(
#                 f"No rows match {E_FAIL_MULTIPLIER_COLUMN} in "
#                 f"{list(multipliers)}."
#             )
#
#         print(
#             f"  {E_FAIL_MULTIPLIER_COLUMN} in {list(multipliers)}: "
#             f"{matching_rows} rows"
#         )
#         values_by_algorithm = collect_algorithm_values(
#             frame,
#             algorithm_names,
#             suffix=WITH_TEMPORAL_REDUNDANCY_SUFFIX,
#             row_mask=row_mask,
#         )
#
#         plot_average_errors(
#             values_by_algorithm,
#             confidence=confidence,
#             output_file=(
#                 output_dir / f"average_error_plot_efail_{group_name}.pdf"
#             ),
#         )
#         plot_relative_errors_vs_reference(
#             values_by_algorithm,
#             reference_name=REFERENCE_ALGORITHM,
#             confidence=confidence,
#             bootstrap_resamples=bootstrap_resamples,
#             seed=seed,
#             output_file=(
#                 output_dir
#                 / f"relative_error_vs_dynamic_efail_{group_name}.pdf"
#             ),
#         )
#
#
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

#
# def report_dynamic_vs_best_heuristic(
#     frame: pd.DataFrame,
#     *,
#     confidence: float,
#     bootstrap_resamples: int,
#     seed: int,
# ) -> None:
#     """Compare the dynamic policy with the selected best static heuristic."""
#     print("\n** COMPARISON BETWEEN DYNAMIC AND BEST HEURISTIC **")
#
#     reference_column = algorithm_column(
#         REFERENCE_ALGORITHM,
#         WITH_TEMPORAL_REDUNDANCY_SUFFIX,
#     )
#     comparison_column = algorithm_column(
#         BEST_HEURISTIC,
#         WITH_TEMPORAL_REDUNDANCY_SUFFIX,
#     )
#     reference, comparison = numeric_pairs(
#         frame,
#         reference_column,
#         comparison_column,
#     )
#
#     print_pair_comparison(
#         reference,
#         comparison,
#         reference_label="Dynamic",
#         comparison_label="best heuristic",
#         confidence=confidence,
#         bootstrap_resamples=bootstrap_resamples,
#         seed=seed,
#     )
#
#
# def report_split_half_stability(
#     frame: pd.DataFrame,
#     algorithm_names: Sequence[str],
#     *,
#     repeats: int = 500,
#     seed: int = DEFAULT_RANDOM_SEED,
#     group_column: str | None = "app_config_id",
# ) -> None:
#     """
#     Assess whether the principal conclusions remain stable when using
#     only half of the independent sampling units.
#
#     When group_column is not None, all rows with the same group value
#     are kept in the same half. Use group_column=None to split rows
#     independently.
#     """
#     if repeats < 1:
#         raise ValueError("repeats must be positive")
#
#     names = list(algorithm_names)
#     columns = [
#         algorithm_column(
#             algorithm_name,
#             WITH_TEMPORAL_REDUNDANCY_SUFFIX,
#         )
#         for algorithm_name in names
#     ]
#
#     require_columns(frame, columns)
#
#     if group_column is not None:
#         require_columns(frame, [group_column])
#
#     # Use exactly the same complete rows for every algorithm so that
#     # rankings are based on identical sets of observations.
#     numeric = frame[columns].apply(
#         pd.to_numeric,
#         errors="coerce",
#     )
#     finite = np.isfinite(numeric.to_numpy()).all(axis=1)
#     values = numeric.loc[finite].to_numpy(dtype=float)
#
#     if values.shape[0] < 4:
#         raise ValueError(
#             "At least four complete observations are required."
#         )
#
#     if group_column is None:
#         groups = None
#         units = np.arange(values.shape[0])
#         unit_description = "rows"
#     else:
#         groups = frame.loc[
#             finite,
#             group_column,
#         ].to_numpy()
#
#         units = pd.unique(groups)
#         unit_description = group_column
#
#     if len(units) < 4:
#         raise ValueError(
#             "At least four independent sampling units are required."
#         )
#
#     names_array = np.asarray(names, dtype=object)
#
#     reference_index = names.index(REFERENCE_ALGORITHM)
#     heuristic_index = names.index(BEST_HEURISTIC)
#
#     def get_row_indices(
#         selected_units: np.ndarray,
#     ) -> np.ndarray:
#         if groups is None:
#             return selected_units.astype(int)
#
#         return np.flatnonzero(
#             np.isin(groups, selected_units)
#         )
#
#     def summarize(
#         row_indices: np.ndarray,
#     ) -> tuple[tuple[str, ...], float]:
#         means = values[row_indices].mean(axis=0)
#
#         ranking = tuple(
#             names_array[np.argsort(means)]
#         )
#
#         heuristic_degradation = 100.0 * (
#             means[heuristic_index]
#             / means[reference_index]
#             - 1.0
#         )
#
#         return ranking, heuristic_degradation
#
#     # Full-data results.
#     all_rows = np.arange(values.shape[0])
#     full_ranking, full_degradation = summarize(all_rows)
#
#     rng = np.random.default_rng(seed)
#
#     half_rankings: list[tuple[str, ...]] = []
#     half_degradations: list[float] = []
#     half_to_half_differences: list[float] = []
#
#     both_halves_dynamic_first = 0
#     both_halves_same_top_two = 0
#
#     for _ in range(repeats):
#         shuffled_units = rng.permutation(units)
#         first_units, second_units = np.array_split(
#             shuffled_units,
#             2,
#         )
#
#         first_ranking, first_degradation = summarize(
#             get_row_indices(first_units)
#         )
#         second_ranking, second_degradation = summarize(
#             get_row_indices(second_units)
#         )
#
#         half_rankings.extend(
#             [first_ranking, second_ranking]
#         )
#         half_degradations.extend(
#             [first_degradation, second_degradation]
#         )
#
#         if (
#             first_ranking[0] == REFERENCE_ALGORITHM
#             and second_ranking[0] == REFERENCE_ALGORITHM
#         ):
#             both_halves_dynamic_first += 1
#
#         if (
#             first_ranking[:2] == full_ranking[:2]
#             and second_ranking[:2] == full_ranking[:2]
#         ):
#             both_halves_same_top_two += 1
#
#         half_to_half_differences.append(
#             abs(first_degradation - second_degradation)
#         )
#
#     dynamic_first_rate = np.mean(
#         [
#             ranking[0] == REFERENCE_ALGORITHM
#             for ranking in half_rankings
#         ]
#     )
#
#     same_top_two_rate = np.mean(
#         [
#             ranking[:2] == full_ranking[:2]
#             for ranking in half_rankings
#         ]
#     )
#
#     same_complete_ranking_rate = np.mean(
#         [
#             ranking == full_ranking
#             for ranking in half_rankings
#         ]
#     )
#
#     half_degradations_array = np.asarray(
#         half_degradations
#     )
#     degradation_low, degradation_median, degradation_high = (
#         np.quantile(
#             half_degradations_array,
#             [0.05, 0.50, 0.95],
#         )
#     )
#
#     print("\n** SPLIT-HALF STABILITY **")
#     print(
#         f"  Independent units: {len(units)} "
#         f"({unit_description})"
#     )
#     print(f"  Random partitions: {repeats}")
#     print(
#         "  Full-data top two: "
#         f"{full_ranking[0]}, {full_ranking[1]}"
#     )
#     print(
#         "  Full-data best-heuristic degradation: "
#         f"{full_degradation:.2f}%"
#     )
#
#     print(
#         "  Dynamic ranked first in half-samples: "
#         f"{100.0 * dynamic_first_rate:.1f}%"
#     )
#     print(
#         "  Full-data top two reproduced in half-samples: "
#         f"{100.0 * same_top_two_rate:.1f}%"
#     )
#     print(
#         "  Complete ranking reproduced in half-samples: "
#         f"{100.0 * same_complete_ranking_rate:.1f}%"
#     )
#     print(
#         "  Both halves ranked dynamic first: "
#         f"{100.0 * both_halves_dynamic_first / repeats:.1f}% "
#         "of partitions"
#     )
#     print(
#         "  Both halves reproduced the full-data top two: "
#         f"{100.0 * both_halves_same_top_two / repeats:.1f}% "
#         "of partitions"
#     )
#     print(
#         "  Half-sample best-heuristic degradation: "
#         f"median {degradation_median:.2f}%, "
#         "central 90% empirical range "
#         f"[{degradation_low:.2f}%, "
#         f"{degradation_high:.2f}%]"
#     )
#     print(
#         "  Median absolute difference between half estimates: "
#         f"{np.median(half_to_half_differences):.2f} "
#         "percentage points"
#     )

def report_independent_temporal_redundancy_results(frame: pd.DataFrame,
                                                   algorithm_names: list[str],
                                                   confidence: float,
                                                   resamples: int,
                                                   seed: int) -> None:
    print("\n** EVALUATION OF THE INDEPENDENT TEMPORAL REDUNDANCY OPTIONS **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        heuristic_options = set({})
        temporal_redundancy_options = set({})
        reactive_rescheduling_options = set({})

        # Identify all the possible algorithm components
        for algorithm_name in algorithm_names:
            heuristic, temporal_redundancy, reactive_rescheduling = algorithm_name.split("__")
            if heuristic == "random":
                continue
            heuristic_options.add(heuristic)
            temporal_redundancy_options.add(temporal_redundancy)
            reactive_rescheduling_options.add(reactive_rescheduling)

        # For a given heuristic and a given reactive rescheduling option,
        # compare all temporal redundancy options, using "independent"
        # as the reference
        num_wins = 0
        num_losses = 0
        num_ties = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        for heuristic in heuristic_options:
            for reactive_rescheduling in reactive_rescheduling_options:
                # print(f"\n{heuristic} + {reactive_rescheduling}:")
                reference_column = algorithm_column(heuristic + "__independent__" + reactive_rescheduling, "")
                for temporal_redundancy in temporal_redundancy_options:
                    if temporal_redundancy == "independent":
                        continue
                    comparison_column = algorithm_column(heuristic + "__" + temporal_redundancy + "__" + reactive_rescheduling, "")
                    reference, comparison = numeric_pairs(tmp_frame, reference_column, comparison_column)

                    estimate, low, high = paired_bootstrap_ratio_of_means(reference, comparison,
                                                                          confidence=confidence, resamples=resamples, seed=seed)
                    if low > 0:
                        num_wins += 1
                        # print(f"   * {heuristic}:{reactive_rescheduling}: independent WINS to {temporal_redundancy}: {100.0 * estimate:.2f}%  [{100.0 * low:.2f}, {100.0 * high:.2f}]")
                        average_win_margin += estimate
                        if estimate > largest_win:
                            largest_win = estimate
                    elif high < 0:
                        num_losses += 1
                        average_loss_margin += estimate
                        # print(f"   * {heuristic}:{reactive_rescheduling}: independent LOSES to {temporal_redundancy}: {100.0 * estimate:.2f}%  [{100.0 * low:.2f}, {100.0 * high:.2f}]")
                        if abs(estimate) > largest_loss:
                            largest_loss = abs(estimate)
                    else:
                        num_ties += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(f"    {num_wins} independent wins  (average win margin {100.0*average_win_margin:.2f}%, largest win: {100.0*largest_win:.2f}%)")
        print(f"    {num_losses} independent losses (average loss margin {100.0*average_loss_margin:.2f}%, largest loss: {100.0*largest_loss:.2f}%)")
        print(f"    {num_ties} independent ties")

    print("    WHEN independent LOSES, IT'S BY VERY LITTLE")



def report_random_heuristic_results(frame: pd.DataFrame,
                                    algorithm_names: list[str],
                                    confidence: float,
                                    resamples: int,
                                    seed: int) -> None:
    print("\n** COMPARISON BETWEEN RANDOM AND ALL OTHER HEURISTICS **")
    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        num_wins = 0
        num_ties = 0
        num_losses = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        count = 0
        for algorithm_name in algorithm_names:
            count +=1
            if "random" in algorithm_name:
                continue
            # if count % 10 == 0:
            #     print(f"{100.0*(count / len(algorithm_names)):.1f}%")

            # print(f"Comparing random to {algorithm_name}")
            comparison_column = algorithm_column(algorithm_name, "")
            tokens = algorithm_name.split("__")
            tokens[0] = "random"
            random_algorithm_name = "__".join(tokens)
            reference_column = algorithm_column(random_algorithm_name, "")
            reference, comparison = numeric_pairs(
                tmp_frame,
                reference_column,
                comparison_column,
            )

            estimate, low, high = paired_bootstrap_ratio_of_means(
                reference,
                comparison,
                confidence=confidence,
                resamples=resamples,
                seed=seed
            )
            # print(f"Comparison to {algorithm_name}: {100.0*low:2f}%/{100.0 * estimate:.2f}%/{100.0*high:2f}%")
            if low > 0:
                num_wins += 1
                average_win_margin += estimate
                if estimate > largest_win:
                    largest_win = estimate
            elif high < 0:
                num_losses += 1
                average_loss_margin += estimate
                if estimate < largest_loss:
                    largest_loss = estimate
            else:
                num_ties += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"    {num_wins} RANDOM wins (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"    {num_losses} RANDOM losses (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"    {num_ties} RANDOM ties")

    print("\n** RANDOM MOSTLY LOSES (IT LIKELY WINS OFTEN AGAINST HEURISTICS THAT HAVE BEEN WEEDED OUT, WHICH WE COULD QUANTIFY) **")


def report_on_greedy_vs_static(frame: pd.DataFrame,
                               algorithm_names: list[str],
                               confidence: float,
                               resamples: int,
                               seed: int) -> None:
    print("\n** COMPARISON BETWEEN THE GREEDY AND THE STATIC HEURISTICS **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        num_wins = 0
        num_ties = 0
        num_losses = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0

        for algorithm_name in algorithm_names:
            if not algorithm_name.startswith("greedy_"):
                continue
            reference_column = algorithm_name
            comparison_column = algorithm_name.replace("greedy_", "static_")
            # print(f"{reference_column} vs. {comparison_column}")

            reference, comparison = numeric_pairs(tmp_frame, reference_column, comparison_column)

            estimate, low, high = paired_bootstrap_ratio_of_means(reference, comparison,
                                                                  confidence=confidence, resamples=resamples, seed=seed)

            if low > 0:
                num_wins += 1
                average_win_margin += estimate
                if estimate > largest_win:
                    largest_win = estimate
            elif high < 0:
                num_losses += 1
                average_loss_margin += estimate
                if abs(estimate) > largest_loss:
                    largest_loss = abs(estimate)
            else:
                num_ties += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"    - {num_wins} GREEDY wins (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"    - {num_losses} GREEDY losses (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"    - {num_ties} GREEDY ties")

    print("GREEDY IS BETTER THAN STATIC")


def compare_two_heuristics(frame: pd.DataFrame,
                           reference_heuristic,
                           comparison_heuristic,
                           confidence: float,
                           resamples: int,
                           seed: int) -> None:
    print(f"\n** COMPARISON OF THE {reference_heuristic} AND THE {comparison_heuristic} HEURISTICS **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        reference, comparison = numeric_pairs(tmp_frame, reference_heuristic, comparison_heuristic)

        estimate, low, high = paired_bootstrap_ratio_of_means(reference, comparison,
                                                              confidence=confidence, resamples=resamples, seed=seed)

        print(
            f"  * {num_nodes} nodes: Estimate={100.0* estimate:.2f} [{100.0 * low:.2f}, {100.0 * high:.2f}]"
        )


def report_on_criterion(frame: pd.DataFrame,
                        algorithm_names: list[str],
                        reference_criterion: str,
                        confidence: float,
                        resamples: int,
                        seed: int) -> None:
    print(f"\n** COMPARISON BETWEEN THE {reference_criterion} CRITERION AND ALL OTHER CRITERIA **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        heuristic_options = set({})
        temporal_redundancy_options = set({})
        reactive_rescheduling_options = set({})

        # Identify all the possible algorithm components
        for algorithm_name in algorithm_names:
            heuristic, temporal_redundancy, reactive_rescheduling = algorithm_name.split("__")
            if heuristic == "random" or heuristic == "dynamic":
                continue
            heuristic_options.add(heuristic)
            temporal_redundancy_options.add(temporal_redundancy)
            reactive_rescheduling_options.add(reactive_rescheduling)

        # Identify all the possible criteria
        criteria = set({})
        for heuristic in heuristic_options:
            tokens = heuristic.split("_")
            heuristic_category = "_".join(tokens[:2])
            criterion = "_".join(tokens[2:])
            criteria.add(criterion)

        num_wins = 0
        num_losses = 0
        num_ties = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        num_times_its_better_than_others = 0
        for temporal_redundancy in temporal_redundancy_options:
            for reactive_rescheduling in reactive_rescheduling_options:
                for heuristic in heuristic_options:
                    # Skip over criteria-less algorithms
                    if "foresighted" not in heuristic and "nearsighted" not in heuristic:
                        continue

                    tokens = heuristic.split("_")
                    heuristic_category = "_".join(tokens[:2])

                    wins_over_all_others = True
                    for criterion in criteria:
                        if criterion == reference_criterion:
                            continue
                        # print(f"{heuristic_category}: compare {reference_criterion} to {criterion}")

                        reference_column = heuristic_category + "_" + reference_criterion + "__" + temporal_redundancy + "__" + reactive_rescheduling
                        comparison_column = heuristic_category + "_" + criterion + "__" + temporal_redundancy + "__" + reactive_rescheduling

                        reference, comparison = numeric_pairs(
                            tmp_frame,
                            reference_column,
                            comparison_column,
                        )

                        estimate, low, high = paired_bootstrap_ratio_of_means(
                            reference,
                            comparison,
                            confidence=confidence,
                            resamples=resamples,
                            seed=seed
                        )

                        # print(f"Comparison to {algorithm_name}: {100.0*low:2f}%/{100.0 * estimate:.2f}%/{100.0*high:2f}%")
                        if high < 0:
                            wins_over_all_others = False

                        if low > 0:
                            num_wins += 1
                            average_win_margin += estimate
                            if estimate > largest_win:
                                largest_win = estimate
                        elif high < 0:
                            num_losses += 1
                            average_loss_margin += estimate
                            if estimate < largest_loss:
                                largest_loss = estimate
                        else:
                            num_ties += 1

                    if wins_over_all_others:
                        num_times_its_better_than_others += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"      {num_wins} {reference_criterion} wins (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"      {num_losses} {reference_criterion} losses (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"      {num_ties} {reference_criterion} ties")

        print(
            f"      Number of times it's better/same than all its competitors, everything else being equal: {num_times_its_better_than_others}"
        )

def compare_two_criteria(frame: pd.DataFrame,
                         algorithm_names: list[str],
                         c1: str,
                         c2: str,
                         confidence: float,
                         resamples: int,
                         seed: int) -> None:
    print(f"\n** COMPARISON BETWEEN THE {c1} AND {c2} CRITERIA **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        heuristic_options = set({})
        temporal_redundancy_options = set({})
        reactive_rescheduling_options = set({})

        # Identify all the possible algorithm components
        for algorithm_name in algorithm_names:
            heuristic, temporal_redundancy, reactive_rescheduling = algorithm_name.split("__")
            if heuristic == "random" or heuristic == "dynamic":
                continue
            heuristic_options.add(heuristic)
            temporal_redundancy_options.add(temporal_redundancy)
            reactive_rescheduling_options.add(reactive_rescheduling)

        num_wins = 0
        num_losses = 0
        num_ties = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        num_times_its_better_than_others = 0
        for algorithm_name in algorithm_names:
            if c1 not in algorithm_name:
                continue


            reference_column = algorithm_name
            comparison_column = algorithm_name.replace(c1, c2)

            reference, comparison = numeric_pairs(
                tmp_frame,
                reference_column,
                comparison_column,
            )

            estimate, low, high = paired_bootstrap_ratio_of_means(
                reference,
                comparison,
                confidence=confidence,
                resamples=resamples,
                seed=seed
            )

            # print(f"Comparison to {algorithm_name}: {100.0*low:2f}%/{100.0 * estimate:.2f}%/{100.0*high:2f}%")
            if low > 0:
                num_wins += 1
                average_win_margin += estimate
                if estimate > largest_win:
                    largest_win = estimate
            elif high < 0:
                num_losses += 1
                average_loss_margin += estimate
                if estimate < largest_loss:
                    largest_loss = estimate
            else:
                num_ties += 1


        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"      {num_wins} {c1} wins over {c2} (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"      {num_losses} {c1} losses to {c2} (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"      {num_ties} {c1} and {c2} ties")

def report_on_foresighted(frame: pd.DataFrame,
                          algorithm_names: list[str],
                          confidence: float,
                          resamples: int,
                          seed: int) -> None:
    print(f"\n** COMPARISON BETWEEN THE FORESIGHTED AND THE NEARSIGHTED HEURISTICS **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        heuristic_options = set({})
        temporal_redundancy_options = set({})
        reactive_rescheduling_options = set({})

        num_wins = 0
        num_losses = 0
        num_ties = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        for algorithm_name in algorithm_names:
            if "foresighted" not in algorithm_name:
                continue

            reference_column = algorithm_name
            comparison_column = algorithm_name.replace("foresighted", "nearsighted")

            reference, comparison = numeric_pairs(
                tmp_frame,
                reference_column,
                comparison_column,
            )

            estimate, low, high = paired_bootstrap_ratio_of_means(
                reference,
                comparison,
                confidence=confidence,
                resamples=resamples,
                seed=seed
            )
            # print(f"Comparison to {algorithm_name}: {100.0*low:2f}%/{100.0 * estimate:.2f}%/{100.0*high:2f}%")
            if low <= 0:
                wins_over_all_others = False

            if low > 0:
                num_wins += 1
                average_win_margin += estimate
                if estimate > largest_win:
                    largest_win = estimate
            elif high < 0:
                num_losses += 1
                average_loss_margin += estimate
                if estimate < largest_loss:
                    largest_loss = estimate
            else:
                num_ties += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"      {num_wins} foresighted wins (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"      {num_losses} foresighted losses (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"      {num_ties} foresighted ties")

        print("  FORESIGHTED WINS MORE OFTEN AND, WHEN IT WINS IT WINS BIG AND WHEN IT LOSES IT LOSES SMALL")


def report_on_reactive(frame: pd.DataFrame,
                       algorithm_names: list[str],
                       reference_reactive: str,
                       confidence: float,
                       resamples: int,
                       seed: int) -> None:
    print(f"\n** COMPARISON BETWEEN THE {reference_reactive} REACTIVE OPTION AND ALL OTHER OPTIONS **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        heuristic_options = set({})
        temporal_redundancy_options = set({})
        reactive_rescheduling_options = set({})

        # Identify all the possible algorithm components
        for algorithm_name in algorithm_names:
            heuristic, temporal_redundancy, reactive_rescheduling = algorithm_name.split("__")
            if heuristic == "random" or heuristic == "dynamic":
                continue
            heuristic_options.add(heuristic)
            temporal_redundancy_options.add(temporal_redundancy)
            reactive_rescheduling_options.add(reactive_rescheduling)

        num_wins = 0
        num_losses = 0
        num_ties = 0
        average_win_margin = 0
        average_loss_margin = 0
        largest_loss = 0
        largest_win = 0
        num_times_its_better_than_others = 0
        for heuristic in heuristic_options:
            for temporal_redundancy in temporal_redundancy_options:
                num_local_wins = 0
                for reactive_rescheduling in reactive_rescheduling_options:

                    if reactive_rescheduling == reference_reactive:
                        continue

                    reference_column = heuristic + "__" + temporal_redundancy + "__" + reference_reactive
                    comparison_column = heuristic + "__" + temporal_redundancy + "__" + reactive_rescheduling

                    reference, comparison = numeric_pairs(
                        tmp_frame,
                        reference_column,
                        comparison_column,
                    )

                    estimate, low, high = paired_bootstrap_ratio_of_means(
                        reference,
                        comparison,
                        confidence=confidence,
                        resamples=resamples,
                        seed=seed
                    )

                    if low > 0:
                        num_local_wins += 1
                        num_wins += 1
                        average_win_margin += estimate
                        if estimate > largest_win:
                            largest_win = estimate
                    elif high < 0:
                        num_losses += 1
                        average_loss_margin += estimate
                        if estimate < largest_loss:
                            largest_loss = estimate
                    else:
                        num_ties += 1

                if num_local_wins == len(reactive_rescheduling_options) - 1:
                    num_times_its_better_than_others += 1

        if num_losses > 0:
            average_loss_margin /= num_losses
        else:
            average_loss_margin = 0
        if num_wins > 0:
            average_win_margin /= num_wins
        else:
            average_win_margin = 0

        print(
            f"      {num_wins} {reference_reactive} wins (average win margin {100.0 * average_win_margin:.2f}%, largest win: {100.0 * largest_win:.2f}%)")
        print(
            f"      {num_losses} {reference_reactive} losses (average loss margin {100.0 * average_loss_margin:.2f}%, largest loss: {100.0 * largest_loss:.2f}%)")
        print(
            f"      {num_ties} {reference_reactive} ties")

        print(
            f"      NUMBER OF TIMES 'off' IS BETTER THAN ALL ITS COMPETITORS, EVERYTHING ELSE BEING EQUAL: {num_times_its_better_than_others}"
        )


def report_algorithm_ranking(frame: pd.DataFrame, algorithm_names: list[str]):

    print(f"\n** ALGORITHM RANKING **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        mean_errors = {}
        for algorithm_name in algorithm_names:
            column_name = algorithm_column(algorithm_name, "")
            values = tmp_frame[column_name].to_numpy(dtype=float)
            mean_error = values.mean()
            # print(f"{column_name}: {values.mean():.2f}")
            mean_errors[column_name] = mean_error
        lowest_mean_error = min(mean_errors.values())
        for algorithm_name, mean_value in mean_errors.items():
            mean_errors[algorithm_name] = (mean_errors[algorithm_name] - lowest_mean_error) / lowest_mean_error

        for key, value in sorted(mean_errors.items(), key=lambda item: item[1]):
            print(f"    {key}: {100.0 * value:.2f}")

#
# Filtering methods
#

def keep_independent_temporal_redundancy_columns(frame: pd.DataFrame,) -> pd.DataFrame:
    """Remove algorithm columns whose middle component is not independent.

    Algorithm columns are expected to have the form A__B__C. Metadata
    columns that do not follow this naming convention are retained.
    """
    columns_to_drop: list[str] = []

    for column in frame.columns:
        components = column.rsplit("__", maxsplit=2)

        # Metadata columns such as app_config_id, num_nodes, etc. do not
        # have the A__B__C form and should be retained.
        if len(components) != 3:
            continue

        _, temporal_redundancy, _ = components

        if temporal_redundancy != "independent":
            columns_to_drop.append(column)

    print(
        f"Removed {len(columns_to_drop)} algorithm columns for non-independent temporal redundancy options"
    )

    return frame.drop(columns=columns_to_drop)

def filter_out_heuristic_with_substring(frame: pd.DataFrame, substring: str, starts_with: bool) -> pd.DataFrame:
    """ Filter out heuristics
    """
    columns_to_drop: list[str] = []

    for column in frame.columns:
        components = column.rsplit("__", maxsplit=2)

        if len(components) != 3:
            continue

        heuristic, _, _ = components

        if starts_with:
            if heuristic.startswith(substring):
                columns_to_drop.append(column)
        else:
            if substring in heuristic:
                columns_to_drop.append(column)

    print(
        f"Removed {len(columns_to_drop)} algorithm columns for heuristic with substring '{substring}'"
    )

    return frame.drop(columns=columns_to_drop)


def filter_out_reactive_option(frame: pd.DataFrame, to_remove: str) -> pd.DataFrame:
    """ Filter out reactive option
    """
    columns_to_drop: list[str] = []

    for column in frame.columns:
        components = column.rsplit("__", maxsplit=2)

        if len(components) != 3:
            continue

        _, _, reactive = components

        if reactive == to_remove:
            columns_to_drop.append(column)

    print(
        f"Removed {len(columns_to_drop)} algorithm columns for the 'off' reactive rescheduling option"
    )

    return frame.drop(columns=columns_to_drop)

def filter_out_efail_multiplier_rows(frame: pd.DataFrame, to_keep: float) -> pd.DataFrame:
    original_row_count = len(frame)

    if E_FAIL_MULTIPLIER_COLUMN not in frame.columns:
        raise KeyError(
            f"CSV file does not contain required column "
            f"{E_FAIL_MULTIPLIER_COLUMN!r}."
        )

    multiplier_values = pd.to_numeric(
        frame[E_FAIL_MULTIPLIER_COLUMN],
        errors="coerce",
    )

    matching_rows = np.isclose(
        multiplier_values.to_numpy(dtype=float),
        to_keep,
        rtol=0.0,
        atol=1e-12,
    )

    frame = frame.loc[matching_rows].copy()

    if frame.empty:
        available_multipliers = sorted(
            multiplier_values.dropna().unique().tolist()
        )
        raise ValueError(
            f"No rows have {E_FAIL_MULTIPLIER_COLUMN} equal to "
            f"{to_keep}. Available values are "
            f"{available_multipliers}."
        )

    print(
        f"Selected {len(frame)} of {original_row_count} rows with "
        f"{E_FAIL_MULTIPLIER_COLUMN}={to_keep:g}"
    )
    return frame




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

    parser.add_argument(
        "--efail-multiplier",
        type=float,
        default=DEFAULT_EFAIL_MULTIPLIER,
        help=f"Efail multiplier to include (default: {DEFAULT_EFAIL_MULTIPLIER}, 0.0 means 'all')",
    )

    parser.add_argument(
        "--temporal-independent-only",
        action="store_true",
        help=(
            "Keep only algorithm columns whose temporal-redundancy "
            "enhancement is 'independent'."
        ),
    )

    parser.add_argument(
        "--exclude-random",
        action="store_true",
        help=(
            "Exclude the 'random' heuristic from all results."
        ),
    )

    parser.add_argument(
        "--exclude-static",
        action="store_true",
        help=(
            "Exclude the 'static_' (as opposed to 'greedy_') heuristics from all results."
        ),
    )

    parser.add_argument(
        "--exclude-nearsighted",
        action="store_true",
        help=(
            "Exclude the 'nearsighted_' (as opposed to 'foresighted_') heuritics from all results."
        ),
    )

    parser.add_argument(
        "--exclude-reactive-off",
        action="store_true",
        help=(
            "Exclude the 'off' reactive rescheduling option from all results."
        ),
    )

    parser.add_argument(
        "--exclude-probability-success",
        action="store_true",
        help=(
            "Exclude the 'probability_success' criterion from all results."
        ),
    )

    return parser


def main() -> None:
    args = build_argument_parser().parse_args()

    if not 0.0 < args.confidence < 1.0:
        raise ValueError("--confidence must be strictly between 0 and 1.")
    if args.bootstrap_resamples < 1:
        raise ValueError("--bootstrap-resamples must be positive.")
    if args.efail_multiplier < 0.0:
        raise ValueError("--efail_multiplier must be non-negative; use 0.0 for all rows.")

    frame = pd.read_csv(args.csv_file)
    print(f"Loaded {len(frame)} rows from {args.csv_file}")

    # Filter out non-matching e-fail values
    if args.efail_multiplier != 0.0:
        frame = filter_out_efail_multiplier_rows(frame, args.efail_multiplier)

    # Filter out non-independent temporal redundancy values
    if args.temporal_independent_only:
        frame = keep_independent_temporal_redundancy_columns(frame)

    # Filter out the random algorithm
    if args.exclude_random:
        frame = filter_out_heuristic_with_substring(frame, "random", True)

    # Filter out the static_* heuristics
    if args.exclude_static:
        frame = filter_out_heuristic_with_substring(frame, "static_", True)

    # Filter out the reactive=off versions
    if args.exclude_reactive_off:
        frame = filter_out_reactive_option(frame, "off")

    # Filter out the nearsighted_ heuristics
    if args.exclude_nearsighted:
        frame = filter_out_heuristic_with_substring(frame, "nearsighted", False)

    # Filter out the error_level criterion
    if args.exclude_probability_success:
        frame = filter_out_heuristic_with_substring(frame, "probability_success", False)

    # Figure out all the algorithms
    algorithm_names = discover_algorithm_names(frame)
    args.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Processing results for {len(algorithm_names)} algorithms")



    if not args.temporal_independent_only:
        report_independent_temporal_redundancy_results(frame,
                                                       algorithm_names,
                                                       args.confidence,
                                                       args.bootstrap_resamples,
                                                       args.seed)
        print("Rerun with --temporal-independent-only to exclude all other temporal redundancy options from any further results")
        sys.exit(0)

    if not args.exclude_static:
        report_on_greedy_vs_static(frame,
                                   algorithm_names,
                                   args.confidence,
                                   args.bootstrap_resamples,
                                   args.seed)
        print("Rerun with --exclude-static to exclude greedy heuristics")
        sys.exit(0)



    if not args.exclude_nearsighted:
        report_on_foresighted(frame,
                              algorithm_names,
                              args.confidence,
                              args.bootstrap_resamples,
                              args.seed)
        print("Rerun with --exclude-nearsighted to exclude the nearsighted heuristics")
        sys.exit(0)


    if not args.exclude_reactive_off:
        report_on_reactive(frame,
                           algorithm_names,
                           "off",
                           args.confidence,
                           args.bootstrap_resamples,
                           args.seed)
        print("Rerun with --exclude-reactive-off to exclude reactive=off")
        sys.exit(0)


    if not args.exclude_probability_success:
        compare_two_criteria(frame,
                             algorithm_names,
                             "expected_error", "probability_success",
                             args.confidence,
                             args.bootstrap_resamples,
                             args.seed)
        print("probability_success IS DOMINATED BY expected_error")
        print("Rerun with --exclude-probability-success to exclude random from any further results")
        sys.exit(0)

    if not args.exclude_random:
        report_random_heuristic_results(frame,
                                        algorithm_names,
                                        args.confidence,
                                        args.bootstrap_resamples,
                                        args.seed)
        print("Rerun with --exclude-random to exclude random from any further results")
        sys.exit(0)

    print("\n*** RANKING AFTER WEEDING OUT CLASSES OF APPROACHES **\n")
    report_algorithm_ranking(frame, algorithm_names)
    print("\n*** FROM THE ABOVE WE CAN DRAW CONCLUSIONS/FINDINGS **\n")

    print("\n** PROVING SOME DIRECT DOMINANCE **")

    for dominating, dominated in [
        ("dynamic__independent__aggressive", "dynamic__independent__variant"),
        ("dynamic__independent__aggressive", "greedy_foresighted_expected_error__independent__aggressive"),
        ("greedy_foresighted_expected_error__independent__variant", "greedy_foresighted_success_error_ratio__independent__variant"),
        ("greedy_foresighted_expected_error__independent__variant", "greedy_foresighted_error_level__independent__variant"),
        ("greedy_foresighted_success_error_ratio__independent__aggressive", "greedy_foresighted_success_error_ratio__independent__variant"),
        ("greedy_foresighted_error_level__independent__aggressive", "greedy_foresighted_success_error_ratio__independent__variant"),
        ("greedy_foresighted_error_level__independent__aggressive", "greedy_foresighted_error_level__independent__variant"),
    ]:

        compare_two_heuristics(frame,
                               dominating,
                               dominated,
                               args.confidence,
                               args.bootstrap_resamples,
                               args.seed)
        if dominated in algorithm_names:
            print(f"    ** REMOVING {dominated} FROM CONSIDERATION **\n")
            algorithm_names.remove(dominated)


    # report_on_criterion(frame,
    #                     algorithm_names,
    #                     "expected_error",
    #                     args.confidence,
    #                     args.bootstrap_resamples,
    #                     args.seed)
    #

    print("\n*** RANKING AFTER WEEDING OUT DOMINATED HEURISTICS **\n")

    report_algorithm_ranking(frame, algorithm_names)

    print("\n*** WE HAVE TO DRAW CONCLUSIONS/FINDINGS FROM THE ABOVE **\n")

if __name__ == "__main__":
    main()
