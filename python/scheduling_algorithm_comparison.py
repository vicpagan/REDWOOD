#!/usr/bin/env python3

import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
import re
import tempfile
import os
from dataclasses import dataclass
from typing import List, Optional, Tuple, Dict
from concurrent.futures import ProcessPoolExecutor, as_completed

DEADLINE_START = 3000
DEADLINE_END   = 15000
DEADLINE_STEP  = 1000

@dataclass
class AlgorithmResult:
    name: str
    comparator: Optional[str]
    deadline: int
    num_repeats: int
    num_successes: int
    success_rate: float
    avg_error: float
    avg_error_successes: float  # may be float('nan') if no successes

def run_simulation(args: Tuple) -> Optional[AlgorithmResult]:
    """Run simulation for a specific algorithm + deadline configuration"""
    json_file, algorithm, comparator, num_repeats, sim_executable, deadline = args

    with open(json_file, 'r') as f:
        config = json.load(f)

    config['execution']['num_repeats'] = num_repeats
    config['execution']['deadline'] = deadline

    if algorithm == "dynamic":
        config['scheduling']['algorithms'] = {
            "one_task": ["dynamic"],
            "multi_task": ["dynamic"]
        }
        config['scheduling']['delta_t_scheme'] = {
            "scheme": "fixed",
            "parameter": 1.0
        }
    elif algorithm in ("static_foresighted", "static_nearsighted"):
        if comparator is None:
            raise ValueError(f"{algorithm} requires a comparator")
        config['scheduling']['algorithms'] = {
            "one_task": [f"{algorithm}_{comparator}"],
            "multi_task": [f"{algorithm}_{comparator}"]
        }
    elif algorithm == "random":
        config['scheduling']['algorithms'] = {
            "one_task": ["random"],
            "multi_task": ["random"]
        }
    else:
        raise ValueError(f"Unknown algorithm: {algorithm}")

    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as tmp:
        json.dump(config, tmp, indent=2)
        tmp_file = tmp.name

    try:
        cmd = [sim_executable, "--json", tmp_file]
        algo_name = f"{algorithm}_{comparator}" if comparator else algorithm
        print(f"Running {algo_name} deadline={deadline}...", flush=True)

        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"FAILED: {algo_name} deadline={deadline}")
            print(f"Error: {result.stderr}", file=sys.stderr)
            return None

        output = result.stdout

        repeats_match        = re.search(r'Total repeats:\s*(\d+)', output)
        successes_match      = re.search(r'Num successes:\s*(\d+)', output)
        success_rate_match   = re.search(r'Success rate:\s*(\d+\.?\d*)', output)
        avg_error_match      = re.search(r'Avg error level:\s*(\d+\.?\d*)', output)
        avg_error_succ_match = re.search(r'Avg error level of successes:\s*(\d+\.?\d*|N/A)', output)

        if not all([repeats_match, successes_match, success_rate_match,
                    avg_error_match, avg_error_succ_match]):
            print(f"Could not parse output for {algo_name} deadline={deadline}")
            print(f"Raw stdout:\n{output}", flush=True)
            print(f"Raw stderr:\n{result.stderr}", flush=True)
            return None

        avg_error_succ_str = avg_error_succ_match.group(1)
        avg_error_successes = float(avg_error_succ_str) if avg_error_succ_str != "N/A" else float('nan')

        result_obj = AlgorithmResult(
            name=algorithm,
            comparator=comparator,
            deadline=deadline,
            num_repeats=int(repeats_match.group(1)),
            num_successes=int(successes_match.group(1)),
            success_rate=float(success_rate_match.group(1)),
            avg_error=float(avg_error_match.group(1)),
            avg_error_successes=avg_error_successes,
        )

        succ_str = f"{avg_error_successes:.3f}" if not np.isnan(avg_error_successes) else "N/A"
        print(f"Done {algo_name} deadline={deadline}: "
              f"success={result_obj.success_rate:.3f} "
              f"err={result_obj.avg_error:.3f} "
              f"err_succ={succ_str}", flush=True)
        return result_obj

    finally:
        os.unlink(tmp_file)


def algo_label(name: str, comparator: Optional[str]) -> str:
    return f"{name}\n({comparator})" if comparator else name

def algo_key(name: str, comparator: Optional[str]) -> str:
    return f"{name}_{comparator}" if comparator else name


def plot_comparison(results: List[AlgorithmResult],
                    deadlines: List[int],
                    configurations: List[Tuple],
                    output_file: str = "algorithm_comparison.png"):

    # Color = comparator (what decision strategy)
    comparator_color_map = {
        None:                  'tab:blue',    # dynamic / random
        'expected_error':      'tab:orange',
        'probability_success': 'tab:green',
        'error_level':         'tab:red',
        'success_error_ratio': 'tab:purple',
    }

    # Linestyle = algorithm family (foresighted vs nearsighted)
    algorithm_linestyle_map = {
        'dynamic':            'solid',
        'static_foresighted': 'solid',
        'static_nearsighted': 'dashed',
        'random':             'dotted',
    }

    # Marker = algorithm family for extra clarity
    algorithm_marker_map = {
        'dynamic':            's',   # square
        'static_foresighted': 'o',   # circle
        'static_nearsighted': '^',   # triangle
        'random':             'D',   # diamond
    }

    # Group results by (algorithm, comparator) -> {deadline -> result}
    grouped: Dict[str, Dict[int, AlgorithmResult]] = {}
    for r in results:
        key = algo_key(r.name, r.comparator)
        if key not in grouped:
            grouped[key] = {}
        grouped[key][r.deadline] = r

    fig, (ax1, ax2, ax3) = plt.subplots(1, 3, figsize=(22, 7))

    for algorithm, comparator in configurations:
        key = algo_key(algorithm, comparator)
        if key not in grouped:
            continue

        data = grouped[key]
        xs = sorted(data.keys())
        success_rates   = [data[d].success_rate * 100 for d in xs]
        avg_errors      = [data[d].avg_error for d in xs]
        avg_errors_succ = [data[d].avg_error_successes for d in xs]

        label  = algo_label(algorithm, comparator)
        color  = comparator_color_map.get(comparator, 'gray')
        ls     = algorithm_linestyle_map.get(algorithm, 'solid')
        marker = algorithm_marker_map.get(algorithm, 'o')

        ax1.plot(xs, success_rates,   label=label, color=color, linestyle=ls, marker=marker, markersize=4)
        ax2.plot(xs, avg_errors,      label=label, color=color, linestyle=ls, marker=marker, markersize=4)
        ax3.plot(xs, avg_errors_succ, label=label, color=color, linestyle=ls, marker=marker, markersize=4)

    for ax, title, ylabel in [
        (ax1, 'Success Rate vs Deadline',               'Success Rate (%)'),
        (ax2, 'Avg Error vs Deadline (All Runs)',        'Average Error Level'),
        (ax3, 'Avg Error vs Deadline (Successes Only)',  'Average Error Level'),
    ]:
        ax.set_title(title, fontsize=13, fontweight='bold')
        ax.set_xlabel('Deadline', fontsize=11)
        ax.set_ylabel(ylabel, fontsize=11)
        ax.grid(True, alpha=0.3)
        ax.set_xticks(deadlines)
        ax.tick_params(axis='x', rotation=45)

    ax1.set_ylim(0, 100)

    # Two-part legend: colors for comparators, linestyles for algorithm families
    from matplotlib.lines import Line2D
    from matplotlib.patches import Patch

    comparator_legend = [
        Patch(color=comparator_color_map[None],                  label='dynamic / random'),
        Patch(color=comparator_color_map['expected_error'],      label='expected_error'),
        Patch(color=comparator_color_map['probability_success'], label='probability_success'),
        Patch(color=comparator_color_map['error_level'],         label='error_level'),
        Patch(color=comparator_color_map['success_error_ratio'], label='success_error_ratio'),
    ]

    style_legend = [
        Line2D([0], [0], color='black', linestyle='solid',  marker='s', markersize=6, label='dynamic'),
        Line2D([0], [0], color='black', linestyle='solid',  marker='o', markersize=6, label='static_foresighted'),
        Line2D([0], [0], color='black', linestyle='dashed', marker='^', markersize=6, label='static_nearsighted'),
        Line2D([0], [0], color='black', linestyle='dotted', marker='D', markersize=6, label='random'),
    ]

    fig.legend(handles=comparator_legend, title='Comparator (color)',
               loc='lower left', bbox_to_anchor=(0.01, -0.18), fontsize=9, ncol=5)
    fig.legend(handles=style_legend, title='Algorithm (line/marker)',
               loc='lower right', bbox_to_anchor=(0.99, -0.18), fontsize=9, ncol=4)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nComparison plot saved to {output_file}")


def print_results_table(results: List[AlgorithmResult], deadlines: List[int]):
    for deadline in deadlines:
        deadline_results = [r for r in results if r.deadline == deadline]
        if not deadline_results:
            continue

        print(f"\n{'='*100}")
        print(f"DEADLINE = {deadline}")
        print(f"{'='*100}")
        print(f"{'Algorithm':<25} {'Comparator':<20} {'Success Rate':<15} {'Avg Error':<15} {'Avg Error (Succ)':<15}")
        print(f"{'-'*100}")

        for r in deadline_results:
            comp_str = r.comparator if r.comparator else "N/A"
            succ_err_str = f"{r.avg_error_successes:>10.4f}" if not np.isnan(r.avg_error_successes) else f"{'N/A':>10}"
            print(f"{r.name:<25} {comp_str:<20} {r.success_rate*100:>6.2f}%{'':<8} "
                  f"{r.avg_error:>10.4f}     {succ_err_str}")

    print(f"\n{'='*100}")
    print("OVERALL BESTS (across all deadlines)")
    print(f"{'='*100}")

    best_success    = max(results, key=lambda r: r.success_rate)
    best_error      = min(results, key=lambda r: r.avg_error)
    valid_succ      = [r for r in results if not np.isnan(r.avg_error_successes)]
    best_error_succ = min(valid_succ, key=lambda r: r.avg_error_successes) if valid_succ else None

    print(f"Best Success Rate: {algo_key(best_success.name, best_success.comparator)} "
          f"@ deadline={best_success.deadline} -> {best_success.success_rate*100:.2f}%")
    print(f"Best Avg Error: {algo_key(best_error.name, best_error.comparator)} "
          f"@ deadline={best_error.deadline} -> {best_error.avg_error:.4f}")
    if best_error_succ:
        print(f"Best Avg Error (Successes): {algo_key(best_error_succ.name, best_error_succ.comparator)} "
              f"@ deadline={best_error_succ.deadline} -> {best_error_succ.avg_error_successes:.4f}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python algorithm_comparison.py <json_file> [num_repeats] [sim_executable]")
        print("\nExample: python algorithm_comparison.py config.json 1000")
        print(f"\nDeadline sweep: {DEADLINE_START} to {DEADLINE_END} step {DEADLINE_STEP}")
        sys.exit(1)

    json_file      = sys.argv[1]
    num_repeats    = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    sim_executable = sys.argv[3] if len(sys.argv) > 3 else "../build/redwood_sim_opt_both"

    if not os.path.exists(json_file):
        print(f"Error: JSON file not found: {json_file}")
        sys.exit(1)

    if not os.path.exists(sim_executable):
        print(f"Error: Simulator executable not found: {sim_executable}")
        sys.exit(1)

    configurations = [
        ("dynamic",            None),
        ("static_foresighted", "expected_error"),
        ("static_foresighted", "probability_success"),
        ("static_foresighted", "error_level"),
        ("static_foresighted", "success_error_ratio"),
        ("static_nearsighted", "expected_error"),
        ("static_nearsighted", "probability_success"),
        ("static_nearsighted", "error_level"),
        ("static_nearsighted", "success_error_ratio"),
        ("random",             None),
    ]

    deadlines = list(range(DEADLINE_START, DEADLINE_END + 1, DEADLINE_STEP))

    all_args = [
        (json_file, algorithm, comparator, num_repeats, sim_executable, deadline)
        for algorithm, comparator in configurations
        for deadline in deadlines
    ]

    total = len(all_args)
    print(f"Starting comparison: {len(configurations)} algorithms x {len(deadlines)} deadlines = {total} jobs")
    print(f"Running all {total} jobs in parallel\n")

    raw_results: Dict[Tuple, Optional[AlgorithmResult]] = {}
    with ProcessPoolExecutor() as executor:
        futures = {executor.submit(run_simulation, args): args for args in all_args}
        for future in as_completed(futures):
            args = futures[future]
            try:
                raw_results[args] = future.result()
            except Exception as e:
                print(f"Job failed for {args[1]}_{args[2]} deadline={args[5]}: {e}", file=sys.stderr)
                raw_results[args] = None

    results = [raw_results[args] for args in all_args if raw_results.get(args) is not None]

    if not results:
        print("No results collected!")
        sys.exit(1)

    print_results_table(results, deadlines)
    plot_comparison(results, deadlines, configurations)

    output_data = {
        "num_repeats": num_repeats,
        "deadlines": deadlines,
        "results": [
            {
                "algorithm":           r.name,
                "comparator":          r.comparator,
                "deadline":            r.deadline,
                "num_successes":       r.num_successes,
                "success_rate":        r.success_rate,
                "avg_error":           r.avg_error,
                "avg_error_successes": None if np.isnan(r.avg_error_successes) else r.avg_error_successes,
            }
            for r in results
        ]
    }

    with open("algorithm_comparison_results.json", "w") as f:
        json.dump(output_data, f, indent=2)
    print("\nDetailed results saved to algorithm_comparison_results.json")


if __name__ == "__main__":
    main()