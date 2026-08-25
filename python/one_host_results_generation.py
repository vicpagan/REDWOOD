#!/usr/bin/env python3
"""Confidence intervals for two paired algorithm columns in a CSV file."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
from scipy.stats import t
import numpy as np
import matplotlib.pyplot as plt
from scipy.stats import t


def count_wins_ties_losses(
        reference: np.ndarray,
        comparison: np.ndarray,
        *,
        rtol: float = 1e-9,
        atol: float = 1e-12,
) -> dict[str, int | float | None]:
    """
    Summarize per-instance wins, ties, and losses.

    Lower values are assumed to be better. Win and loss counts are from the
    reference algorithm's perspective. Conditional relative gains and losses
    are reported from the comparison algorithm's perspective, always using
    the reference value as the denominator:

        comparison loss = (comparison - reference) / reference
        comparison gain = (reference - comparison) / reference

    The first quantity is averaged only over instances where the comparison
    algorithm loses; the second is averaged only over instances where it wins.

    Two values are considered tied when

        abs(reference - comparison)
            <= atol + rtol * max(abs(reference), abs(comparison))

    Parameters
    ----------
    reference:
        Error values produced by the reference algorithm.
    comparison:
        Error values produced by the comparison algorithm.
    rtol:
        Relative tolerance used to identify ties.
    atol:
        Absolute tolerance used to identify ties.

    Returns
    -------
    A dictionary containing the numbers of reference wins, ties, and reference
    losses, plus the comparison algorithm's mean relative degradation when it
    loses and mean relative improvement when it wins. A conditional mean is
    None if there are no observations in the corresponding category.
    """
    reference = np.asarray(reference, dtype=float)
    comparison = np.asarray(comparison, dtype=float)

    if reference.shape != comparison.shape:
        raise ValueError(
            "reference and comparison must have the same shape"
        )

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
        values: np.ndarray, confidence: float
) -> tuple[float, float, float]:
    """Return (sample mean, lower bound, upper bound) for a t interval."""
    n = values.size
    if n < 2:
        raise ValueError("At least two paired observations are required.")

    estimate = float(np.mean(values))
    standard_error = float(np.std(values, ddof=1) / np.sqrt(n))
    alpha = 1.0 - confidence
    critical_value = float(t.ppf(1.0 - alpha / 2.0, df=n - 1))
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
    """Percentile-bootstrap CI for E[comparison]/E[reference] - 1.

    Rows are resampled as pairs, preserving the paired experimental design.
    """
    if resamples < 1:
        raise ValueError("The number of bootstrap resamples must be positive.")

    n = reference.size
    rng = np.random.default_rng(seed)
    bootstrap_estimates = np.empty(resamples, dtype=float)

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
        bootstrap_estimates, [alpha / 2.0, 1.0 - alpha / 2.0]
    )
    estimate = float(comparison.mean() / reference.mean() - 1.0)
    return estimate, float(low), float(high)

def plot_average_errors(
        values_by_algorithm: dict[str, list[float]],
        confidence: float = 0.95,
        output_file: str | None = None,
) -> None:
    """
    Plot mean error and confidence interval for each algorithm.

    Algorithms are sorted by increasing mean error, so the best algorithm
    appears at the top. Lower error is assumed to be better.
    """
    if not 0.0 < confidence < 1.0:
        raise ValueError("confidence must be strictly between 0 and 1")

    results = []

    for algorithm_name, values in values_by_algorithm.items():
        values_array = np.asarray(values, dtype=float)

        # Remove NaN and infinite values.
        values_array = values_array[np.isfinite(values_array)]

        if values_array.size < 2:
            raise ValueError(
                f"{algorithm_name!r} has fewer than two valid values."
            )

        mean = float(np.mean(values_array))

        standard_error = float(
            np.std(values_array, ddof=1)
            / np.sqrt(values_array.size)
        )

        alpha = 1.0 - confidence
        critical_value = float(
            t.ppf(
                1.0 - alpha / 2.0,
                df=values_array.size - 1,
                )
        )

        confidence_interval_half_width = (
                critical_value * standard_error
        )

        results.append(
            (
                algorithm_name,
                mean,
                confidence_interval_half_width,
                values_array.size,
            )
        )

    # Sort by increasing mean error.
    results.sort(key=lambda result: result[1])

    algorithm_names = [result[0] for result in results]
    means = np.array([result[1] for result in results])
    error_bars = np.array([result[2] for result in results])

    # Increase the figure height when there are many algorithms.
    figure_height = max(4.0, 0.55 * len(algorithm_names))

    fig, ax = plt.subplots(
        figsize=(10, figure_height)
    )

    ax.barh(
        algorithm_names,
        means,
        xerr=error_bars,
        capsize=4,
    )

    # Because results are sorted in increasing order, put the smallest
    # mean at the top rather than at the bottom.
    ax.invert_yaxis()

    confidence_percent = 100.0 * confidence

    ax.set_xlabel("Mean error")
    ax.set_ylabel("Algorithm")
    ax.set_title(
        f"Mean error by algorithm "
        f"({confidence_percent:g}% confidence intervals)"
    )

    # Horizontal grid lines would cross the bars; vertical lines help
    # compare their lengths.
    ax.grid(axis="x", alpha=0.3)

    # Add the numerical mean and confidence interval next to each bar.
    for y_position, (mean, half_width) in enumerate(
            zip(means, error_bars)
    ):
        lower = mean - half_width
        upper = mean + half_width

        ax.text(
            upper,
            y_position,
            f"  {mean:.2f} [{lower:.2f}, {upper:.2f}]",
            va="center",
            ha="left",
        )

    # Leave some room on the right for the numerical labels.
    ax.margins(x=0.20)

    fig.tight_layout()

    if output_file is not None:
        fig.savefig(
            output_file,
            dpi=300,
            bbox_inches="tight",
        )
        print("  Saved plot to", output_file)


def plot_relative_errors_vs_reference(
        values_by_algorithm: dict[str, list[float]],
        reference_name: str = "dynamic",
        confidence: float = 0.95,
        bootstrap_resamples: int = 20_000,
        seed: int = 20260821,
        output_file: str | None = None,
) -> None:
    """
    Plot each algorithm's relative difference in mean error compared
    with a reference algorithm.

    Positive values mean that the algorithm has higher mean error than
    the reference and is therefore worse.
    """
    if reference_name not in values_by_algorithm:
        raise KeyError(
            f"Reference algorithm {reference_name!r} is not present."
        )

    reference_all = np.asarray(
        values_by_algorithm[reference_name],
        dtype=float,
    )

    results = []

    for algorithm_name, algorithm_values in values_by_algorithm.items():
        if algorithm_name == reference_name:
            continue

        comparison_all = np.asarray(
            algorithm_values,
            dtype=float,
        )

        if reference_all.shape != comparison_all.shape:
            raise ValueError(
                f"{algorithm_name!r} and {reference_name!r} "
                "do not have the same number of values."
            )

        # Keep only rows for which both algorithms have finite values.
        finite = (
                np.isfinite(reference_all)
                & np.isfinite(comparison_all)
        )

        reference = reference_all[finite]
        comparison = comparison_all[finite]

        if reference.size < 2:
            raise ValueError(
                f"Not enough valid paired values for "
                f"{algorithm_name!r}."
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

    # Matplotlib expects distances from the point estimate, not bounds.
    asymmetric_error_bars = np.vstack(
        [
            estimates - lower_bounds,
            upper_bounds - estimates,
            ]
    )

    y_positions = np.arange(len(algorithm_names))

    figure_height = max(4.0, 0.55 * len(algorithm_names))
    fig, ax = plt.subplots(figsize=(10, figure_height))

    ax.errorbar(
        estimates,
        y_positions,
        xerr=asymmetric_error_bars,
        fmt="o",
        capsize=4,
    )

    # Zero means equal mean error to the reference algorithm.
    ax.axvline(0.0, linewidth=1.0)

    ax.set_yticks(y_positions)
    ax.set_yticklabels(algorithm_names)
    ax.invert_yaxis()

    ax.set_xlabel(
        f"Increase in mean error relative to {reference_name} (%)"
    )
    ax.set_ylabel("Algorithm")
    ax.set_title(
        f"Relative mean error compared with {reference_name} "
        f"({100.0 * confidence:g}% paired-bootstrap intervals)"
    )

    ax.grid(axis="x", alpha=0.3)

    fig.tight_layout()

    if output_file is not None:
        fig.savefig(
            output_file,
            dpi=300,
            bbox_inches="tight",
        )
        print("  Saved plot to", output_file)



def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Compare two paired algorithm columns in a CSV file. "
            "Lower values are assumed to be better."
        )
    )
    parser.add_argument("csv_file", help="Input CSV file")
    args = parser.parse_args()

    frame = pd.read_csv(args.csv_file)
    total_rows = len(frame)
    column_names = frame.columns.tolist()
    # print(column_names)
    algorithm_names = list(set([name.split('__')[0] for name in column_names[9:-5]]))

    # Determining the impact of temporal redundancy
    print("\n** IMPROVEMENT DUE TO TEMPORAL REDUNDANCY **")
    perc_improvement_tr = {}
    for algorithm_name in algorithm_names:
        # Ignore the random algorithm
        if algorithm_name == "random":
            continue
        algorithm_without_temporal_redundancy = algorithm_name+"__off__off"
        algorithm_with_temporal_redundancy = algorithm_name+"__independent__off"
        pairs = frame[[algorithm_without_temporal_redundancy, algorithm_with_temporal_redundancy]].apply(
            pd.to_numeric, errors="coerce"
        )
        finite = np.isfinite(pairs.to_numpy()).all(axis=1)
        pairs = pairs.loc[finite]

        if pairs.empty:
            raise ValueError(
                "The selected row range contains no complete finite pairs."
            )

        without_tr = pairs[algorithm_without_temporal_redundancy].to_numpy(dtype=float)
        with_tr = pairs[algorithm_with_temporal_redundancy].to_numpy(dtype=float)
        mean_without = without_tr.mean()
        mean_with = with_tr.mean()
        relative_improvement = 100.0* (mean_without - mean_with) / mean_without
        perc_improvement_tr[algorithm_name] = relative_improvement
    for algorithm_name, perc_improvement in sorted(
            perc_improvement_tr.items(),
            key=lambda item: item[1]):
        print(f"  - {algorithm_name}: {perc_improvement:.2f}%")
    print(f" Average improvement: {sum(perc_improvement_tr.values()) / len(perc_improvement_tr):.2f}%")

    # Generate average error plots for our own purposes for different e-fails
    print("\n** AVERAGE ERRORS **")
    efail_values = {"2.0": [2.0], "5.0": [5.0], "any": [2.0,5.0]}
    for efail_key, efail_values in efail_values.items():
        result_dict = {}
        for algorithm_name in algorithm_names:
            algorithm = algorithm_name+"__independent__off"
            error_values = frame.loc[
                frame["e_fail_multiplier"].isin(efail_values),
                algorithm,
            ].tolist()
            result_dict[algorithm_name] = error_values
        plot_average_errors(result_dict, confidence=.95,
                            output_file="average_error_plot_efail_" + str(efail_key) + ".pdf")
        plot_relative_errors_vs_reference(result_dict,reference_name="dynamic", confidence=0.95,
                                          output_file="relative_error_vs_dynamic_efail_" + str(efail_key) + ".pdf")


    # Talk about how far the best heuristic is from optimal: impact of efail, impact of difficulty
    # Interpretation 2: percentage difference between the two population means.
    print("\n** COMPARISON BETWEEN DYNAMIC AND BEST HEURISTIC **")
    reference_column = "dynamic__independent__off"
    comparison_column = "static_foresighted_expected_error__independent__off"
    pairs = frame[[reference_column, comparison_column]].apply(
        pd.to_numeric, errors="coerce"
    )
    finite = np.isfinite(pairs.to_numpy()).all(axis=1)
    pairs = pairs.loc[finite]

    if pairs.empty:
        raise ValueError(
            "The selected row range contains no complete finite pairs."
        )

    reference = pairs[reference_column].to_numpy(dtype=float)
    comparison = pairs[comparison_column].to_numpy(dtype=float)
    estimate2, low2, high2 = paired_bootstrap_ratio_of_means(
        reference, comparison,
        confidence=0.95, resamples=20_000, seed=20260821)
    print()
    print("  Relative difference between the two means")
    print("    theta = E[comparison] / E[reference] - 1")
    print(f"   Estimate: {100.0 * estimate2:.4f}%")
    print(
        f"    {95:g}% paired-bootstrap interval: "
        f"[{100.0 * low2:.4f}%, {100.0 * high2:.4f}%]"
    )

    # Numbers of wins / losses / ties and conditional effect sizes.
    counts = count_wins_ties_losses(reference, comparison)
    comparison_degradation = counts[
        "mean_comparison_degradation_when_losing"
    ]
    comparison_improvement = counts[
        "mean_comparison_improvement_when_winning"
    ]

    print()
    print("  Per-instance comparison")
    print(
        f"  Dynamic wins / best-heuristic losses: "
        f"{counts['wins']:6d}"
    )
    if comparison_degradation is None:
        print("    Mean comparison degradation: n/a (no comparison losses)")
    else:
        print(
            "    Mean comparison degradation when losing: "
            f"{100.0 * comparison_degradation:.4f}%"
        )

    print(
        f"  Dynamic losses / best-heuristic wins: "
        f"{counts['losses']:6d}"
    )
    if comparison_improvement is None:
        print("    Mean comparison improvement: n/a (no comparison wins)")
    else:
        print(
            "    Mean comparison improvement when winning: "
            f"{100.0 * comparison_improvement:.4f}%"
        )

    print(f"  Ties:                              {counts['ties']:6d}")





    # parser.add_argument(
    #     "reference_algorithm",
    #     metavar="REFERENCE_ALGORITHM",
    #     help="Name of the CSV column for the reference algorithm",
    # )
    # parser.add_argument(
    #     "comparison_algorithm",
    #     metavar="COMPARISON_ALGORITHM",
    #     help="Name of the CSV column for the comparison algorithm",
    # )
    # parser.add_argument(
    #     "--start-row",
    #     type=int,
    #     default=0,
    #     help=(
    #         "Index of the first CSV row to process (0-based, inclusive). "
    #         "Default: 0"
    #     ),
    # )
    # parser.add_argument(
    #     "--end-row",
    #     type=int,
    #     default=None,
    #     help=(
    #         "Index one past the last CSV row to process (0-based, exclusive). "
    #         "Default: total number of rows"
    #     ),
    # )
    # parser.add_argument(
    #     "--confidence", type=float, default=0.95, help="Confidence level"
    # )
    # parser.add_argument(
    #     "--bootstrap-resamples",
    #     type=int,
    #     default=20_000,
    #     help="Bootstrap resamples for the ratio-of-means interval",
    # )
    # parser.add_argument("--seed", type=int, default=20260821)
    # args = parser.parse_args()

    # if not 0.0 < args.confidence < 1.0:
    #     raise ValueError("--confidence must be strictly between 0 and 1.")
    #
    # if args.start_row < 0:
    #     raise ValueError("--start-row must be nonnegative.")
    #
    # reference_column = args.reference_algorithm
    # comparison_column = args.comparison_algorithm
    #
    # if reference_column == comparison_column:
    #     raise ValueError(
    #         "The reference and comparison algorithms must be different."
    #     )
    #
    # frame = pd.read_csv(args.csv_file)
    # total_rows = len(frame)
    #
    # end_row = total_rows if args.end_row is None else args.end_row
    # if end_row < 0:
    #     raise ValueError("--end-row must be nonnegative.")
    # if args.start_row > end_row:
    #     raise ValueError("--start-row cannot be greater than --end-row.")
    # if args.start_row > total_rows:
    #     raise ValueError(
    #         f"--start-row ({args.start_row}) is beyond the total number "
    #         f"of rows ({total_rows})."
    #     )
    # if end_row > total_rows:
    #     raise ValueError(
    #         f"--end-row ({end_row}) is beyond the total number of rows "
    #         f"({total_rows})."
    #     )
    #
    # frame = frame.iloc[args.start_row:end_row]
    #
    # missing_columns = {
    #     reference_column,
    #     comparison_column,
    # } - set(frame.columns)
    # if missing_columns:
    #     raise KeyError(f"Missing CSV column(s): {sorted(missing_columns)}")
    #
    # pairs = frame[[reference_column, comparison_column]].apply(
    #     pd.to_numeric, errors="coerce"
    # )
    # finite = np.isfinite(pairs.to_numpy()).all(axis=1)
    # pairs = pairs.loc[finite]
    #
    # if pairs.empty:
    #     raise ValueError(
    #         "The selected row range contains no complete finite pairs."
    #     )
    #
    # reference = pairs[reference_column].to_numpy(dtype=float)
    # comparison = pairs[comparison_column].to_numpy(dtype=float)
    #
    # if np.any(reference <= 0.0):
    #     count = int(np.count_nonzero(reference <= 0.0))
    #     raise ValueError(
    #         f"The reference column contains {count} nonpositive value(s); "
    #         "a relative difference with this denominator is not meaningful."
    #     )
    #
    # confidence_percent = 100.0 * args.confidence
    #
    # # Interpretation 1: average the paired, per-instance percentage changes.
    # per_instance_relative_difference = (comparison - reference) / reference
    # estimate, low, high = mean_t_confidence_interval(
    #     per_instance_relative_difference, args.confidence
    # )
    #
    # print(f"Total rows in CSV: {total_rows}")
    # print(
    #     f"Requested row range: [{args.start_row}, {end_row}) "
    #     f"({end_row - args.start_row} rows)"
    # )
    # print(f"Usable paired rows: {reference.size}")
    # print(f"Reference algorithm:  {reference_column}")
    # print(f"Comparison algorithm: {comparison_column}")
    # print(f"Mean {reference_column}: {reference.mean():.9g}")
    # print(f"Mean {comparison_column}: {comparison.mean():.9g}")
    # print()
    # print("Mean of the per-instance relative differences")
    # print("  r_i = (comparison_i - reference_i) / reference_i")
    # print(f"  Estimate: {100.0 * estimate:.4f}%")
    # print(
    #     f"  {confidence_percent:g}% t confidence interval: "
    #     f"[{100.0 * low:.4f}%, {100.0 * high:.4f}%]"
    # )
    #
    # # Interpretation 2: percentage difference between the two population means.
    # estimate2, low2, high2 = paired_bootstrap_ratio_of_means(
    #     reference,
    #     comparison,
    #     confidence=args.confidence,
    #     resamples=args.bootstrap_resamples,
    #     seed=args.seed,
    # )
    # print()
    # print("Relative difference between the two means")
    # print("  theta = E[comparison] / E[reference] - 1")
    # print(f"  Estimate: {100.0 * estimate2:.4f}%")
    # print(
    #     f"  {confidence_percent:g}% paired-bootstrap interval: "
    #     f"[{100.0 * low2:.4f}%, {100.0 * high2:.4f}%]"
    # )
    #
    # # Numbers of wins / losses / ties and conditional effect sizes.
    # counts = count_wins_ties_losses(reference, comparison)
    # comparison_degradation = counts[
    #     "mean_comparison_degradation_when_losing"
    # ]
    # comparison_improvement = counts[
    #     "mean_comparison_improvement_when_winning"
    # ]
    #
    # print()
    # print("Per-instance comparison")
    # print(
    #     f"  Reference wins / comparison losses: "
    #     f"{counts['wins']:6d}"
    # )
    # if comparison_degradation is None:
    #     print("    Mean comparison degradation: n/a (no comparison losses)")
    # else:
    #     print(
    #         "    Mean comparison degradation when losing: "
    #         f"{100.0 * comparison_degradation:.4f}%"
    #     )
    #
    # print(
    #     f"  Reference losses / comparison wins: "
    #     f"{counts['losses']:6d}"
    # )
    # if comparison_improvement is None:
    #     print("    Mean comparison improvement: n/a (no comparison wins)")
    # else:
    #     print(
    #         "    Mean comparison improvement when winning: "
    #         f"{100.0 * comparison_improvement:.4f}%"
    #     )
    #
    # print(f"  Ties:                              {counts['ties']:6d}")


if __name__ == "__main__":
    main()
