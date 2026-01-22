#!/usr/bin/env python3

import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
import re
import tempfile
import os

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

def run_simulation(json_file, delta_t, num_repeats=1000, sim_executable="../build/redwood_sim"):
    """Run simulation for a specific delta_t value"""
    # Load the original JSON
    with open(json_file, 'r') as f:
        config = json.load(f)

    # Modify the config for this delta_t
    config['scheduling']['delta_t_scheme'] = {
        "scheme": "fixed",
        "parameter": delta_t
    }
    config['execution']['num_repeats'] = num_repeats

    # Write to a temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as tmp:
        json.dump(config, tmp, indent=2)
        tmp_file = tmp.name

    try:
        # Run the simulation
        cmd = [sim_executable, "--json", tmp_file]
        print(f"  Running simulation for delta_t={delta_t}...", end=' ', flush=True)
        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode != 0:
            print(f"FAILED")
            print(f"Simulation error: {result.stderr}", file=sys.stderr)
            return None

        # Parse the output to extract avg error level
        output = result.stdout
        match = re.search(r'Avg error level:\s*(\d+\.?\d*)', output)

        if match:
            avg_error = float(match.group(1))
            print(f"Avg error: {avg_error:.3f}")
            return avg_error
        else:
            print(f"Could not parse avg error from output")
            return None

    finally:
        # Clean up temp file
        os.unlink(tmp_file)

def plot_results(data, sim_results=None, output_file="delta_t_analysis.png"):
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

    # Plot simulation results if available
    lines_for_legend = line1 + line2
    if sim_results:
        sim_delta_t = []
        sim_avg_error = []
        for dt, err in sim_results.items():
            if err is not None:
                sim_delta_t.append(dt)
                sim_avg_error.append(err)

        if sim_delta_t:
            line4 = ax1.plot(sim_delta_t, sim_avg_error, 'D', color='purple',
                             markersize=8, label='Actual simulation (1000 runs)',
                             markeredgecolor='black', markeredgewidth=0.5)
            lines_for_legend = lines_for_legend + line4

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
    lines = lines_for_legend + line3
    labels = [l.get_label() for l in lines]
    ax1.legend(lines, labels, loc='best', fontsize=10)

    # Add title and annotations
    plt.title('Delta_t Impact on Expected Error and Computation Time',
              fontsize=14, fontweight='bold', pad=20)

    # Add annotation for optimal delta_t (smallest upper bound)
    optimal_idx = np.argmin(upper_bound)
    optimal_delta_t = delta_t[optimal_idx]
    optimal_error = upper_bound[optimal_idx]

    # ax1.annotate(f'Optimal delta_t = {optimal_delta_t:.2f}\nError = {optimal_error:.3f}',
    #              xy=(optimal_delta_t, optimal_error),
    #              xytext=(optimal_delta_t + (max(delta_t) - min(delta_t)) * 0.1,
    #                      optimal_error * 1.1),
    #              arrowprops=dict(arrowstyle='->', color='red', lw=2),
    #              fontsize=10, bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.8))

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
        print("Usage: python delta_t_evaluator.py <json_file> <min_delta_t> <max_delta_t> <step_size> [executable] [--run-sims] [--sim-executable <path>]")
        print("Example: python delta_t_evaluator.py config.json 0.5 10.0 0.5")
        print("         python delta_t_evaluator.py config.json 1.0 10.0 1.0 ./delta_t_evaluator --run-sims")
        sys.exit(1)

    json_file = sys.argv[1]
    min_delta_t = float(sys.argv[2])
    max_delta_t = float(sys.argv[3])
    step_size = float(sys.argv[4])

    # Parse remaining arguments
    executable = "./delta_t_evaluator"
    run_sims = False
    sim_executable = "../build/redwood_sim"

    i = 5
    while i < len(sys.argv):
        if sys.argv[i] == "--run-sims":
            run_sims = True
            i += 1
        elif sys.argv[i] == "--sim-executable":
            sim_executable = sys.argv[i + 1]
            i += 2
        else:
            executable = sys.argv[i]
            i += 1

    # Run evaluator
    data = run_evaluator(json_file, min_delta_t, max_delta_t, step_size, executable)

    # Run simulations if requested
    sim_results = None
    if run_sims:
        print("\n=== Running Simulations ===")
        sim_results = {}
        delta_t_values = data["delta_t"]

        for dt in delta_t_values:
            avg_error = run_simulation(json_file, dt, num_repeats=50, sim_executable=sim_executable)
            sim_results[dt] = avg_error

        print("\nSimulation results:")
        for dt, err in sim_results.items():
            if err is not None:
                print(f"  delta_t={dt}: avg_error={err:.3f}")

    # Create visualization
    plot_results(data, sim_results)

    # Optionally save raw data
    output_data = data.copy()
    if sim_results:
        output_data["simulation_avg_error"] = [sim_results.get(dt) for dt in data["delta_t"]]

    with open("delta_t_results.json", "w") as f:
        json.dump(output_data, f, indent=2)
    print("Raw data saved to delta_t_results.json")

if __name__ == "__main__":
    main()