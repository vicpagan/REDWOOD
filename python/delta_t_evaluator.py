#!/usr/bin/env python3

import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np

def run_evaluator(json_file, min_delta_t, max_delta_t, step_size, executable="./delta_t_evaluator"):
    """Run the C++ evaluator and parse results"""
    cmd = [
        executable,
        "--json", json_file,
        "--min_delta_t", str(min_delta_t),
        "--max_delta_t", str(max_delta_t),
        "--step_size", str(step_size)
    ]

    print(f"Running: {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"Error running evaluator: {result.stderr}", file=sys.stderr)
        sys.exit(1)

    return json.loads(result.stdout)

def plot_results(data, output_file="delta_t_analysis.png"):
    """Create visualization of delta_t analysis"""
    delta_t = np.array(data["delta_t"])
    upper_bound = np.array(data["upper_bound"])
    lower_bound = np.array(data["lower_bound"])
    comp_time = np.array(data["computation_time_ms"])

    # Create figure with two y-axes
    fig, ax1 = plt.subplots(figsize=(12, 6))

    # Plot expected error on left y-axis
    color1 = 'tab:red'
    color2 = 'tab:blue'
    ax1.set_xlabel('Delta_t (time discretization)', fontsize=12)
    ax1.set_ylabel('Expected Error', color='black', fontsize=12)

    line1 = ax1.plot(delta_t, upper_bound, 'o-', color=color1, linewidth=2,
                     markersize=6, label='Upper Bound (pessimistic)')
    line2 = ax1.plot(delta_t, lower_bound, 's-', color=color2, linewidth=2,
                     markersize=6, label='Lower Bound (optimistic)')

    # Fill area between bounds
    ax1.fill_between(delta_t, lower_bound, upper_bound, alpha=0.2, color='gray',
                     label='Uncertainty range')

    ax1.tick_params(axis='y', labelcolor='black')
    ax1.grid(True, alpha=0.3)

    # Plot computation time on right y-axis
    ax2 = ax1.twinx()
    color3 = 'tab:green'
    ax2.set_ylabel('Computation Time (ms)', color=color3, fontsize=12)
    line3 = ax2.plot(delta_t, comp_time, '^-', color=color3, linewidth=2,
                     markersize=6, label='Preprocessing time')
    ax2.tick_params(axis='y', labelcolor=color3)

    # Combine legends
    lines = line1 + line2 + line3
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc='best', fontsize=10)

    # Add title and annotations
    plt.title('Delta_t Impact on Expected Error and Computation Time',
              fontsize=14, fontweight='bold', pad=20)

    # Add annotation for optimal delta_t (smallest upper bound)
    optimal_idx = np.argmin(upper_bound)
    optimal_delta_t = delta_t[optimal_idx]
    optimal_error = upper_bound[optimal_idx]

    ax1.annotate(f'Optimal delta_t = {optimal_delta_t:.2f}\nError = {optimal_error:.3f}',
                 xy=(optimal_delta_t, optimal_error),
                 xytext=(optimal_delta_t + (max(delta_t) - min(delta_t)) * 0.1,
                         optimal_error * 1.1),
                 arrowprops=dict(arrowstyle='->', color='red', lw=2),
                 fontsize=10, bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8))

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to {output_file}")

    # Also show statistics
    print("\n=== Delta_t Analysis Summary ===")
    print(f"Delta_t range: [{min(delta_t):.2f}, {max(delta_t):.2f}]")
    print(f"Optimal delta_t (min upper bound): {optimal_delta_t:.2f}")
    print(f"  Expected error: {optimal_error:.3f}")
    print(f"  Computation time: {comp_time[optimal_idx]:.0f} ms")
    print(f"\nBounds spread at optimal delta_t: {(upper_bound[optimal_idx] - lower_bound[optimal_idx]):.3f}")
    print(f"Relative error: {100 * (upper_bound[optimal_idx] - lower_bound[optimal_idx]) / upper_bound[optimal_idx]:.2f}%")

    return fig

def main():
    if len(sys.argv) < 5:
        print("Usage: python delta_t_evaluator.py <json_file> <min_delta_t> <max_delta_t> <step_size> [executable]")
        print("Example: python delta_t_evaluator.py config.json 0.5 10.0 0.5")
        sys.exit(1)

    json_file = sys.argv[1]
    min_delta_t = float(sys.argv[2])
    max_delta_t = float(sys.argv[3])
    step_size = float(sys.argv[4])
    executable = sys.argv[5] if len(sys.argv) > 5 else "./delta_t_evaluator"

    # Run evaluator
    data = run_evaluator(json_file, min_delta_t, max_delta_t, step_size, executable)

    # Create visualization
    plot_results(data)

    # Optionally save raw data
    with open("delta_t_results.json", "w") as f:
        json.dump(data, f, indent=2)
    print("Raw data saved to delta_t_results.json")

if __name__ == "__main__":
    main()