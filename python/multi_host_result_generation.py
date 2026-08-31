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


def report_independent_temporal_redundancy_results(frame: pd.DataFrame,
                                                   algorithm_names: list[str],
                                                   confidence: float,
                                                   resamples: int,
                                                   seed: int) -> None:
    print("\n** EVALUATION OF THE INDEPENDENT TEMPORAL REDUNDANCY OPTION **")

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
            f"      Number of times 'off' is better than all its competitors, everything else being equal: {num_times_its_better_than_others}"
        )


def report_algorithm_ranking(frame: pd.DataFrame, algorithm_names: list[str]):

    print(f"\n** ALGORITHM RANKING **")

    # Identify all number of nodes
    num_nodes_values = sorted(list(set(frame["num_nodes"].to_numpy(dtype=int))))
    for num_nodes in num_nodes_values:
        print(f"  * {num_nodes} nodes:")
        matching_rows = np.equal(frame["num_nodes"], num_nodes)
        tmp_frame = frame.loc[matching_rows].copy()

        # Compute mean error dfb
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



        # Compute relative different with best-case
        distance_from_optimal = {}
        optimal_values = tmp_frame["min_error"].to_numpy(dtype=float)
        for algorithm_name in algorithm_names:
            column_name = algorithm_column(algorithm_name, "")
            values = tmp_frame[column_name].to_numpy(dtype=float)
            dfo = 0
            for idx in range(len(values)):
                dfo += (values[idx] - optimal_values[idx]) / optimal_values[idx]
            dfo /= len(values)
            distance_from_optimal[algorithm_name] = dfo

        for key, value in sorted(mean_errors.items(), key=lambda item: item[1]):
            print(f"    {key+":":64} dfb={100.0 * value:.2f}\tdfo={100.0 * distance_from_optimal[key]:.2f}")

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


    # Exclusion arguments
    ########################
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

    # Evaluation arguments
    ########################
    parser.add_argument(
        "--evaluate-temporal-independent",
        action="store_true",
        help=(
            "Obtain results that show that 'independent' temporal-redundancy "
            "is 'better' than all other options"
        ),
    )

    parser.add_argument(
        "--evaluate-greedy",
        action="store_true",
        help=(
            "Show results to show that 'greedy' is better than 'static'"
        ),
    )

    parser.add_argument(
        "--evaluate-foresighted",
        action="store_true",
        help=(
            "Show results to show that 'foresighted' is better than 'nearsighted'"
        ),
    )

    parser.add_argument(
        "--evaluate-reactive-off",
        action="store_true",
        help=(
            "Show results to show that 'off' reactive rescheduling is not a good idea"
        ),
    )

    parser.add_argument(
        "--evaluate-probability-success",
        action="store_true",
        help=(
            "Show results to show that the 'probability_success' criterion is not a good idea"
        ),
    )

    parser.add_argument(
        "--evaluate-random",
        action="store_true",
        help=(
            "Show results to show that 'random' is not a good idea"
        ),
    )

    parser.add_argument(
        "--evaluate-reactive-aggressive",
        action="store_true",
        help=(
            "Show results for reactive aggressive"
        ),
    )

    parser.add_argument(
        "--show-ranking",
        action="store_true",
        help=(
            "Show algorithm ranking"
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

    if not args.temporal_independent_only and args.evaluate_temporal_independent:
        report_independent_temporal_redundancy_results(frame,
                                                       algorithm_names,
                                                       args.confidence,
                                                       args.bootstrap_resamples,
                                                       args.seed)

    if not args.exclude_static and args.evaluate_greedy:
        report_on_greedy_vs_static(frame,
                                   algorithm_names,
                                   args.confidence,
                                   args.bootstrap_resamples,
                                   args.seed)

    if not args.exclude_nearsighted and args.evaluate_foresighted:
        report_on_foresighted(frame,
                              algorithm_names,
                              args.confidence,
                              args.bootstrap_resamples,
                              args.seed)

    if not args.exclude_reactive_off and args.evaluate_reactive_off:
        report_on_reactive(frame,
                           algorithm_names,
                           "off",
                           args.confidence,
                           args.bootstrap_resamples,
                           args.seed)

    if args.evaluate_reactive_aggressive:
        report_on_reactive(frame,
                           algorithm_names,
                           "aggressive",
                           args.confidence,
                           args.bootstrap_resamples,
                           args.seed)

    if not args.exclude_probability_success and args.evaluate_probability_success:
        compare_two_criteria(frame,
                             algorithm_names,
                             "expected_error", "probability_success",
                             args.confidence,
                             args.bootstrap_resamples,
                             args.seed)
        print("probability_success IS DOMINATED BY expected_error")

    if not args.exclude_random and args.evaluate_random:
        report_random_heuristic_results(frame,
                                        algorithm_names,
                                        args.confidence,
                                        args.bootstrap_resamples,
                                        args.seed)

    if args.show_ranking:
        report_algorithm_ranking(frame, algorithm_names)

        if args.temporal_independent_only and args.exclude_static and args.exclude_reactive_off and args.exclude_nearsighted and args.exclude_probability_success and args.exclude_nearsighted:

            print("\n** PROVING SOME DIRECT DOMINANCE: WARNING HARD-CODED ALGORITHM NAMES BASED ON KNOWN RESULTS**")
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
    
    
                print("\n*** RANKING AFTER WEEDING OUT DOMINATED HEURISTICS **\n")
    
                report_algorithm_ranking(frame, algorithm_names)

if __name__ == "__main__":
    main()
