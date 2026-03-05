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
from typing import List, Optional, Tuple
from concurrent.futures import ProcessPoolExecutor, as_completed

@dataclass
class AlgorithmResult:
    name: str
    num_repeats: int
    num_successes: int
    success_rate: float
    avg_error: float
    comparator: Optional[str] = None

def run_simulation(args: Tuple) -> Optional[AlgorithmResult]:
    """Run simulation for a specific algorithm configuration"""
    json_file, algorithm, comparator, num_repeats, sim_executable = args

    # Load the original JSON
    with open(json_file, 'r') as f:
        config = json.load(f)

    config['execution']['num_repeats'] = num_repeats

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
        print(f"Running {algo_name} ({num_repeats} repeats)...", flush=True)

        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"FAILED: {algo_name}")
            print(f"Error: {result.stderr}", file=sys.stderr)
            return None

        output = result.stdout

        repeats_match = re.search(r'Total repeats:\s*(\d+)', output)
        successes_match = re.search(r'Num successes:\s*(\d+)', output)
        success_rate_match = re.search(r'Success rate:\s*(\d+\.?\d*)', output)
        avg_error_match = re.search(r'Avg error level:\s*(\d+\.?\d*)', output)

        if not all([repeats_match, successes_match, success_rate_match, avg_error_match]):
            print(f"Could not parse output for {algo_name}")
            return None

        result_obj = AlgorithmResult(
            name=algorithm,
            num_repeats=int(repeats_match.group(1)),
            num_successes=int(successes_match.group(1)),
            success_rate=float(success_rate_match.group(1)),
            avg_error=float(avg_error_match.group(1)),
            comparator=comparator
        )

        print(f"Done {algo_name}: Success rate={result_obj.success_rate:.3f}, Avg error={result_obj.avg_error:.3f}", flush=True)
        return result_obj

    finally:
        os.unlink(tmp_file)


def plot_comparison(results: List[AlgorithmResult], output_file: str = "algorithm_comparison.png"):
    """Create comparison plots"""

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))

    labels = []
    success_rates = []
    avg_errors = []
    colors = []

    color_map = {
        'dynamic': 'tab:blue',
        'static_foresighted': 'tab:orange',
        'static_nearsighted': 'tab:purple',
        'random': 'tab:green'
    }

    for result in results:
        if result.comparator:
            label = f"{result.name}\n({result.comparator})"
        else:
            label = result.name
        labels.append(label)
        success_rates.append(result.success_rate * 100)
        avg_errors.append(result.avg_error)
        colors.append(color_map.get(result.name, 'gray'))

    x = np.arange(len(labels))

    # Plot 1: Success Rate
    bars1 = ax1.bar(x, success_rates, color=colors, alpha=0.7, edgecolor='black')
    ax1.set_ylabel('Success Rate (%)', fontsize=12)
    ax1.set_title('Success Rate Comparison', fontsize=14, fontweight='bold')
    ax1.set_xticks(x)
    ax1.set_xticklabels(labels, rotation=45, ha='right', fontsize=9)
    ax1.grid(True, alpha=0.3, axis='y')
    ax1.set_ylim(0, 100)

    for bar in bars1:
        height = bar.get_height()
        ax1.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.1f}%', ha='center', va='bottom', fontsize=9)

    # Plot 2: Average Error
    bars2 = ax2.bar(x, avg_errors, color=colors, alpha=0.7, edgecolor='black')
    ax2.set_ylabel('Average Error Level', fontsize=12)
    ax2.set_title('Average Error Comparison', fontsize=14, fontweight='bold')
    ax2.set_xticks(x)
    ax2.set_xticklabels(labels, rotation=45, ha='right', fontsize=9)
    ax2.grid(True, alpha=0.3, axis='y')

    for bar in bars2:
        height = bar.get_height()
        ax2.text(bar.get_x() + bar.get_width()/2., height,
                 f'{height:.2f}', ha='center', va='bottom', fontsize=9)

    from matplotlib.patches import Patch
    legend_elements = [Patch(facecolor=color, alpha=0.7, edgecolor='black', label=name)
                       for name, color in color_map.items()
                       if any(r.name == name for r in results)]
    fig.legend(handles=legend_elements, loc='upper right', fontsize=10)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"\nComparison plot saved to {output_file}")


def print_results_table(results: List[AlgorithmResult]):
    """Print results in a formatted table"""
    print("\n" + "="*80)
    print("ALGORITHM COMPARISON RESULTS")
    print("="*80)
    print(f"{'Algorithm':<25} {'Comparator':<20} {'Success Rate':<15} {'Avg Error':<15}")
    print("-"*80)

    for result in results:
        comp_str = result.comparator if result.comparator else "N/A"
        print(f"{result.name:<25} {comp_str:<20} {result.success_rate*100:>6.2f}%{'':<8} {result.avg_error:>10.4f}")

    print("="*80)

    best_success = max(results, key=lambda r: r.success_rate)
    best_error = min(results, key=lambda r: r.avg_error)

    print(f"\nBest Success Rate: {best_success.name}", end='')
    if best_success.comparator:
        print(f" ({best_success.comparator})", end='')
    print(f" - {best_success.success_rate*100:.2f}%")

    print(f"Best Avg Error: {best_error.name}", end='')
    if best_error.comparator:
        print(f" ({best_error.comparator})", end='')
    print(f" - {best_error.avg_error:.4f}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python algorithm_comparison.py <json_file> [num_repeats] [sim_executable]")
        print("\nExample: python algorithm_comparison.py config.json 1000")
        print("\nThis will compare:")
        print("  - Dynamic scheduling")
        print("  - Static foresighted scheduling with different comparators:")
        print("      * expected_error")
        print("      * probability_success")
        print("      * error_level")
        print("      * success_error_ratio")
        print("  - Static nearsighted scheduling with different comparators:")
        print("      * expected_error")
        print("      * probability_success")
        print("      * error_level")
        print("      * success_error_ratio")
        print("  - Random scheduling")
        sys.exit(1)

    json_file = sys.argv[1]
    num_repeats = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    sim_executable = sys.argv[3] if len(sys.argv) > 3 else "../build/redwood_sim_opt_both"

    if not os.path.exists(json_file):
        print(f"Error: JSON file not found: {json_file}")
        sys.exit(1)

    if not os.path.exists(sim_executable):
        print(f"Error: Simulator executable not found: {sim_executable}")
        sys.exit(1)

    configurations = [
        ("dynamic", None),
        ("static_foresighted", "expected_error"),
        ("static_foresighted", "probability_success"),
        ("static_foresighted", "error_level"),
        ("static_foresighted", "success_error_ratio"),
        ("static_nearsighted", "expected_error"),
        ("static_nearsighted", "probability_success"),
        ("static_nearsighted", "error_level"),
        ("static_nearsighted", "success_error_ratio"),
        ("random", None),
    ]

    all_args = [(json_file, algorithm, comparator, num_repeats, sim_executable)
                for algorithm, comparator in configurations]

    print(f"Starting algorithm comparison with {num_repeats} repeats per algorithm")
    print(f"Running {len(configurations)} configurations in parallel\n")

    # Run all configurations in parallel
    raw_results = {}
    with ProcessPoolExecutor() as executor:
        futures = {executor.submit(run_simulation, args): args for args in all_args}
        for future in as_completed(futures):
            args = futures[future]
            raw_results[args] = future.result()

    # Reorder results to match original configuration order
    results = [raw_results[args] for args in all_args if raw_results.get(args) is not None]

    if not results:
        print("No results collected!")
        sys.exit(1)

    print_results_table(results)
    plot_comparison(results)

    output_data = {
        "num_repeats": num_repeats,
        "results": [
            {
                "algorithm": r.name,
                "comparator": r.comparator,
                "num_successes": r.num_successes,
                "success_rate": r.success_rate,
                "avg_error": r.avg_error
            }
            for r in results
        ]
    }

    with open("algorithm_comparison_results.json", "w") as f:
        json.dump(output_data, f, indent=2)
    print("\nDetailed results saved to algorithm_comparison_results.json")


if __name__ == "__main__":
    main()