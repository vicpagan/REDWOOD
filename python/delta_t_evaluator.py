#!/usr/bin/env python3

import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
import re
import tempfile
import os

def get_executable_names(base_evaluator, base_sim, optimistic_exec, optimistic_sched):
    """Get the appropriate executable names based on flags"""
    suffix = ""
    if optimistic_exec and optimistic_sched:
        suffix = "_opt_both"
    elif optimistic_exec:
        suffix = "_opt_exec"
    elif optimistic_sched:
        suffix = "_opt_sched"

    # Add suffix before file extension or at end
    evaluator = base_evaluator.replace("delta_t_evaluator", f"delta_t_evaluator{suffix}")
    sim = base_sim.replace("redwood_sim", f"redwood_sim{suffix}")

    return evaluator, sim

def run_evaluator(json_file, min_delta_t, max_delta_t, step_size, executable):
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

def run_simulation(json_file, delta_t, num_repeats, sim_executable):
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

def plot_results(all_results, output_file="delta_t_analysis.png"):
    """Create visualization of delta_t analysis"""
    # all_results is a dict with keys: 'default', 'opt_exec', 'opt_sched', 'opt_both'
    # Each contains: data (with delta_t, upper_bound, lower_bound, comp_time), sim_results

    fig, ax1 = plt.subplots(figsize=(14, 7))

    # Colors and markers for different configurations
    configs = {
        'default': {'color_u': 'tab:red', 'color_l': 'tab:blue', 'marker_u': 'o', 'marker_l': 's',
                    'label_prefix': 'Default', 'sim_color': 'purple', 'sim_marker': 'D'},
        'opt_exec': {'color_u': 'indianred', 'color_l': 'lightblue', 'marker_u': 'v', 'marker_l': '^',
                     'label_prefix': 'Opt Exec', 'sim_color': 'orchid', 'sim_marker': 'p'},
        'opt_sched': {'color_u': 'darkred', 'color_l': 'darkblue', 'marker_u': '<', 'marker_l': '>',
                      'label_prefix': 'Opt Sched', 'sim_color': 'mediumorchid', 'sim_marker': 'h'},
        'opt_both': {'color_u': 'orangered', 'color_l': 'steelblue', 'marker_u': 'p', 'marker_l': 'H',
                     'label_prefix': 'Opt Both', 'sim_color': 'darkviolet', 'sim_marker': '*'}
    }

    ax1.set_xlabel('Delta_t (time discretization)', fontsize=12)
    ax1.set_ylabel('Expected Error', color='black', fontsize=12)

    all_lines = []

    # Plot each configuration
    for config_key, result in all_results.items():
        if result is None:
            continue

        data = result['data']
        sim_results = result['sim_results']
        cfg = configs[config_key]

        delta_t = np.array(data["delta_t"])
        upper_bound = np.array(data["upper_bound"])
        lower_bound = np.array(data["lower_bound"])

        # Plot bounds
        line_u = ax1.plot(delta_t, upper_bound, cfg['marker_u'] + '-',
                          color=cfg['color_u'], linewidth=1.5, markersize=5,
                          label=f"{cfg['label_prefix']} Upper", alpha=0.8)
        line_l = ax1.plot(delta_t, lower_bound, cfg['marker_l'] + '-',
                          color=cfg['color_l'], linewidth=1.5, markersize=5,
                          label=f"{cfg['label_prefix']} Lower", alpha=0.8)
        all_lines.extend(line_u + line_l)

        # Fill between bounds
        ax1.fill_between(delta_t, lower_bound, upper_bound, alpha=0.05, color=cfg['color_u'])

        # Plot simulation results if available
        if sim_results:
            sim_delta_t = []
            sim_avg_error = []
            for dt, err in sim_results.items():
                if err is not None:
                    sim_delta_t.append(dt)
                    sim_avg_error.append(err)

            if sim_delta_t:
                line_sim = ax1.plot(sim_delta_t, sim_avg_error, cfg['sim_marker'],
                                    color=cfg['sim_color'], markersize=7,
                                    label=f"{cfg['label_prefix']} Sim",
                                    markeredgecolor='black', markeredgewidth=0.5, alpha=0.9)
                all_lines.extend(line_sim)

    ax1.tick_params(axis='y', labelcolor='black')
    ax1.grid(True, alpha=0.3)

    # Plot computation time on right y-axis (only from default)
    if 'default' in all_results and all_results['default'] is not None:
        ax2 = ax1.twinx()
        comp_time = np.array(all_results['default']['data']["computation_time_ms"])
        delta_t = np.array(all_results['default']['data']["delta_t"])

        color3 = 'tab:green'
        ax2.set_ylabel('Computation Time (ms)', color=color3, fontsize=12)
        line_time = ax2.plot(delta_t, comp_time, '^-', color=color3, linewidth=2,
                             markersize=6, label='Preprocessing time', alpha=0.7)
        ax2.tick_params(axis='y', labelcolor=color3)
        all_lines.extend(line_time)

    # Combined legend
    labels = [l.get_label() for l in all_lines]
    ax1.legend(all_lines, labels, loc='best', fontsize=8, ncol=2)

    # Add title
    plt.title('Delta_t Impact on Expected Error (Multiple Configurations)',
              fontsize=14, fontweight='bold', pad=20)

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to {output_file}")

    return fig

def main():
    if len(sys.argv) < 5:
        print("Usage: python delta_t_evaluator.py <json_file> <min_delta_t> <max_delta_t> <step_size> [options]")
        print("\nOptions:")
        print("  --executable <path>          Path to delta_t_evaluator executable (default: ./delta_t_evaluator)")
        print("  --sim-executable <path>      Path to simulation executable (default: ../build/redwood_sim)")
        print("  --run-sims                   Run actual simulations")
        print("  --num-repeats <n>            Number of simulation repeats (default: 1000)")
        print("  --compare-all                Run all configurations (default + all optimistic variants)")
        print("\nExample:")
        print("  python delta_t_evaluator.py config.json 1.0 10.0 1.0 --run-sims")
        print("  python delta_t_evaluator.py config.json 1.0 10.0 1.0 --compare-all --run-sims")
        print("\nNote: Requires pre-built executables with suffixes:")
        print("  delta_t_evaluator, delta_t_evaluator_opt_exec, delta_t_evaluator_opt_sched, delta_t_evaluator_opt_both")
        print("  redwood_sim, redwood_sim_opt_exec, redwood_sim_opt_sched, redwood_sim_opt_both")
        sys.exit(1)

    json_file = sys.argv[1]
    min_delta_t = float(sys.argv[2])
    max_delta_t = float(sys.argv[3])
    step_size = float(sys.argv[4])

    # Parse remaining arguments
    base_executable = "./delta_t_evaluator"
    base_sim_executable = "../build/redwood_sim"
    run_sims = False
    num_repeats = 1000
    compare_all = False

    i = 5
    while i < len(sys.argv):
        if sys.argv[i] == "--run-sims":
            run_sims = True
            i += 1
        elif sys.argv[i] == "--executable":
            base_executable = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--sim-executable":
            base_sim_executable = sys.argv[i + 1]
            i += 2
        elif sys.argv[i] == "--num-repeats":
            num_repeats = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "--compare-all":
            compare_all = True
            i += 1
        else:
            i += 1

    # Determine which configurations to run
    if compare_all:
        configurations = [
            ('default', False, False),
            ('opt_exec', True, False),
            ('opt_sched', False, True),
            ('opt_both', True, True)
        ]
    else:
        configurations = [('default', False, False)]

    all_results = {}

    for config_name, opt_exec, opt_sched in configurations:
        print(f"\n{'='*60}")
        print(f"Running configuration: {config_name}")
        print(f"{'='*60}")

        # Get the appropriate executables for this configuration
        evaluator_exe, sim_exe = get_executable_names(base_executable, base_sim_executable,
                                                      opt_exec, opt_sched)

        # Check if executables exist
        if not os.path.exists(evaluator_exe):
            print(f"ERROR: Executable not found: {evaluator_exe}")
            print(f"Please build with: make")
            sys.exit(1)

        # Run evaluator
        data = run_evaluator(json_file, min_delta_t, max_delta_t, step_size, evaluator_exe)

        # Run simulations if requested
        sim_results = None
        if run_sims:
            if not os.path.exists(sim_exe):
                print(f"ERROR: Simulation executable not found: {sim_exe}")
                print(f"Skipping simulations for {config_name}")
            else:
                print("\n=== Running Simulations ===")
                sim_results = {}
                delta_t_values = data["delta_t"]

                for dt in delta_t_values:
                    avg_error = run_simulation(json_file, dt, num_repeats, sim_exe)
                    sim_results[dt] = avg_error

                print("\nSimulation results:")
                for dt, err in sim_results.items():
                    if err is not None:
                        print(f"  delta_t={dt}: avg_error={err:.3f}")

        all_results[config_name] = {
            'data': data,
            'sim_results': sim_results
        }

    # Create visualization
    plot_results(all_results)

    # Save raw data
    output_data = {}
    for config_name, result in all_results.items():
        output_data[config_name] = result['data'].copy()
        if result['sim_results']:
            output_data[config_name]["simulation_avg_error"] = [
                result['sim_results'].get(dt) for dt in result['data']["delta_t"]
            ]

    with open("delta_t_results.json", "w") as f:
        json.dump(output_data, f, indent=2)
    print("\nRaw data saved to delta_t_results.json")

if __name__ == "__main__":
    main()