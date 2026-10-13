#!/usr/bin/env python3
"""Generate summary statistics and plot-ready JSON for one-host results."""

from __future__ import annotations

import argparse
import json
from collections.abc import Mapping, Sequence
from pathlib import Path
from typing import TypedDict

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


class AverageErrorResult(TypedDict):
    """One algorithm's mean error and Student-t confidence interval."""

    algorithm: str
    label: str
    num_observations: int
    mean: float
    ci_lower: float
    ci_upper: float


class RelativeErrorResult(TypedDict):
    """A paired comparison with the reference; all effect sizes are percent."""

    algorithm: str
    label: str
    num_pairs: int
    estimate_percent: float
    ci_lower_percent: float
    ci_upper_percent: float


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
) -> (float, float, float):
    """Return (mean(baseline) - mean(improved)) / mean(baseline)."""
    baseline_mean = float(np.mean(baseline))
    improved_mean = float(np.mean(improved))

    if baseline_mean <= 0.0:
        raise ValueError("The baseline mean must be positive.")

    improvement = (baseline_mean - improved_mean) / baseline_mean

    return improvement, baseline_mean, improved_mean


def compute_average_errors(
    values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
    *,
    confidence: float = DEFAULT_CONFIDENCE,
) -> list[AverageErrorResult]:
    """Compute each algorithm's mean error and t confidence interval."""
    results: list[AverageErrorResult] = []

    for algorithm_name, values in values_by_algorithm.items():
        numeric_values = np.asarray(values, dtype=float)
        mean, low, high = mean_t_confidence_interval(
            numeric_values,
            confidence,
        )
        results.append({
            "algorithm": algorithm_name,
            "label": ALGORITHM_NAME_MAP[algorithm_name],
            "num_observations": int(np.count_nonzero(np.isfinite(numeric_values))),
            "mean": mean,
            "ci_lower": low,
            "ci_upper": high,
        })

    # Lower error is better; preserve CSV order when means are equal.
    results.sort(key=lambda result: result["mean"])
    return results


def compute_relative_errors_vs_reference(
    values_by_algorithm: Mapping[str, Sequence[float] | np.ndarray],
    *,
    reference_name: str = REFERENCE_ALGORITHM,
    confidence: float = DEFAULT_CONFIDENCE,
    bootstrap_resamples: int = DEFAULT_BOOTSTRAP_RESAMPLES,
    seed: int = DEFAULT_RANDOM_SEED,
) -> list[RelativeErrorResult]:
    """Compute paired-bootstrap relative mean errors, expressed as percentages.

    Positive values mean that an algorithm has a higher mean error than the
    reference and is therefore worse. Each comparison uses its own finite
    pairs, exactly as in the original plotting function.
    """
    if reference_name not in values_by_algorithm:
        raise KeyError(
            f"Reference algorithm {reference_name!r} is not present."
        )

    reference_all = np.asarray(
        values_by_algorithm[reference_name],
        dtype=float,
    )
    results: list[RelativeErrorResult] = []

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
        results.append({
            "algorithm": algorithm_name,
            "label": ALGORITHM_NAME_MAP[algorithm_name],
            "num_pairs": int(reference.size),
            "estimate_percent": 100.0 * estimate,
            "ci_lower_percent": 100.0 * low,
            "ci_upper_percent": 100.0 * high,
        })

    # Put the closest competitors first, as in the original script.
    results.sort(key=lambda result: result["estimate_percent"])
    return results


def write_json(data: object, output_file: Path) -> None:
    """Write indented, standards-compliant JSON, creating parent directories."""
    # Serialize first so invalid data cannot truncate an existing output file.
    serialized = json.dumps(data, indent=2, allow_nan=False) + "\n"
    output_file.parent.mkdir(parents=True, exist_ok=True)
    output_file.write_text(serialized, encoding="utf-8")
    print(f"  Saved JSON to {output_file}")


def report_temporal_redundancy_improvements(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
) -> None:
    """Print the mean-error improvement due to temporal redundancy."""
    print("\n** IMPROVEMENT DUE TO TEMPORAL REDUNDANCY **")
    improvements: dict[str, (float, float, float)] = {}

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
        improvement, without_tr, with_tr = relative_improvement_between_means(without_tr, with_tr)
        improvements[algorithm_name] = (100.0 * improvement, without_tr, with_tr)

    for algorithm_name, (improvement, without_tr, with_tr) in sorted(
        improvements.items(),
        key=lambda item: item[1],
    ):
        print(f"  - {algorithm_name}: {improvement:.2f}%  (without tr: {without_tr:.2f}, with tr: {with_tr:.2f})")

    if improvements:
        print(
            "  Average improvement across algorithms: "
            f"{np.mean(list([x[0] for x in improvements.values()])):.2f}%"
        )


def generate_average_error_json(
    frame: pd.DataFrame,
    algorithm_names: Sequence[str],
    *,
    source_csv: Path,
    average_errors_output: Path,
    relative_errors_output: Path,
    confidence: float,
    bootstrap_resamples: int,
    seed: int,
) -> None:
    """Write two plot-ready JSON files, each containing all failure groups.

    Store final estimates and interval endpoints, not the raw observations:
    plotting these files never needs to recompute confidence intervals.
    """
    print("\n** AVERAGE ERRORS **")
    require_columns(frame, [E_FAIL_MULTIPLIER_COLUMN])

    average_groups = {}
    relative_groups = {}
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
        group_metadata = {
            "filter": {E_FAIL_MULTIPLIER_COLUMN: list(multipliers)},
            "num_rows": matching_rows,
        }
        average_groups[group_name] = {
            **group_metadata,
            "results": compute_average_errors(
                values_by_algorithm,
                confidence=confidence,
            ),
        }
        relative_groups[group_name] = {
            **group_metadata,
            "results": compute_relative_errors_vs_reference(
                values_by_algorithm,
                reference_name=REFERENCE_ALGORITHM,
                confidence=confidence,
                bootstrap_resamples=bootstrap_resamples,
                seed=seed,
            ),
        }

    common_metadata = {
        "schema_version": 1,
        "source_csv": str(source_csv),
        "algorithm_suffix": WITH_TEMPORAL_REDUNDANCY_SUFFIX,
        "confidence": confidence,
    }
    write_json({
        **common_metadata,
        "plot_type": "average_errors",
        "confidence_interval_method": "student_t",
        "groups": average_groups,
    }, average_errors_output)
    write_json({
        **common_metadata,
        "plot_type": "relative_errors",
        "reference_algorithm": REFERENCE_ALGORITHM,
        "reference_label": ALGORITHM_NAME_MAP[REFERENCE_ALGORITHM],
        "statistic": "100 * (mean(comparison) / mean(reference) - 1)",
        "units": "percent",
        "confidence_interval_method": "paired_percentile_bootstrap",
        "bootstrap_resamples": bootstrap_resamples,
        "seed": seed,
        "groups": relative_groups,
    }, relative_errors_output)


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
            "Generate statistics and two plot-ready JSON files for one-host "
            "scheduling results. Lower error values are assumed to be better. "
            "No plots are generated by this script."
        ),
        allow_abbrev=False,
    )
    required = parser.add_argument_group("required arguments")
    required.add_argument(
        "--csv-input", required=True, type=Path, metavar="PATH",
        help="Input CSV file.",
    )
    required.add_argument(
        "--output-average-errors-json", required=True, type=Path, metavar="PATH",
        help="Output JSON file for mean errors and t confidence intervals.",
    )
    required.add_argument(
        "--output-relative-errors-json", required=True, type=Path, metavar="PATH",
        help="Output JSON file for relative errors and paired-bootstrap intervals.",
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


def main(argv: Sequence[str] | None = None) -> None:
    parser = build_argument_parser()
    args = parser.parse_args(argv)

    if not 0.0 < args.confidence < 1.0:
        parser.error("--confidence must be strictly between 0 and 1.")
    if args.bootstrap_resamples < 1:
        parser.error("--bootstrap-resamples must be positive.")
    if args.seed < 0:
        parser.error("--seed must be nonnegative.")

    paths = [
        args.csv_input.resolve(),
        args.output_average_errors_json.resolve(),
        args.output_relative_errors_json.resolve(),
    ]
    if len(set(paths)) != len(paths):
        parser.error("The input CSV and the two output JSON paths must be distinct.")

    try:
        frame = pd.read_csv(args.csv_input)
        algorithm_names = discover_algorithm_names(frame)

        print(f"Loaded {len(frame)} rows from {args.csv_input}")
        print(f"Discovered {len(algorithm_names)} algorithms")

        report_temporal_redundancy_improvements(frame, algorithm_names)
        generate_average_error_json(
            frame,
            algorithm_names,
            source_csv=args.csv_input,
            average_errors_output=args.output_average_errors_json,
            relative_errors_output=args.output_relative_errors_json,
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
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
