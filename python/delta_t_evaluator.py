#!/usr/bin/env python3

import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
import re
import tempfile
import os
import math

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

def force_required_config_values(config):
    """Force the config to be compatible with the delta_t evaluator:
    - exactly 1 compute node
    - the only scheduling algorithm is 'dynamic'
    - temporal_redundancy and stop_running_jobs hacks are both 'off'

    Mutates and returns the given config dict in place.
    """
    config.setdefault('platform', {})
    config['platform']['num_compute_nodes'] = 1

    config.setdefault('scheduling', {})
    config['scheduling']['algorithms'] = ["dynamic"]

    config['scheduling'].setdefault('hacks', {})
    config['scheduling']['hacks']['temporal_redundancy'] = "off"
    config['scheduling']['hacks']['stop_running_jobs'] = "off"

    return config

def prepare_config_file(json_file):
    """Load json_file, force the required values into it, and write the
    result to a temp file. Returns the path to the temp file."""
    with open(json_file, 'r') as f:
        config = json.load(f)

    force_required_config_values(config)

    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as tmp:
        json.dump(config, tmp, indent=2)
        return tmp.name

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

def run_simulation(json_file, delta_t, num_repeats, seed, sim_executable):
    """Run simulation for a specific delta_t value"""
    # Load the original JSON
    with open(json_file, 'r') as f:
        config = json.load(f)

    # Modify the config for this delta_t.
    # NOTE: the current config schema nests the scheduling block under
    # "scheduling" -> "delta_t_scheme" and also carries extra metadata
    # ("delta_t_scheme_comments", "parameter_comments") as well as sibling
    # keys ("algorithms", "hacks") that must be preserved rather than
    # clobbered. We therefore update the existing dict in place instead of
    # replacing "scheduling" (or "delta_t_scheme") wholesale.
    config.setdefault('scheduling', {})
    existing_scheme = config['scheduling'].get('delta_t_scheme', {})

    updated_scheme = dict(existing_scheme)  # keep any *_comments fields intact
    updated_scheme['scheme'] = 'fixed'
    updated_scheme['parameter'] = delta_t

    config['scheduling']['delta_t_scheme'] = updated_scheme

    # "num_repeats" lives under "execution" in the current schema
    config.setdefault('execution', {})
    config['execution']['num_repeats'] = num_repeats

    # Force the same required values as the evaluator config: 1 host,
    # dynamic-only scheduling, both hacks off
    force_required_config_values(config)

    # Write to a temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as tmp:
        json.dump(config, tmp, indent=2)
        tmp_file = tmp.name

    try:
        # Run the simulation
        cmd = [sim_executable, "--json", tmp_file, "--seed", str(seed), "--fake_io"]
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
        else:
            print(f"Could not parse avg error from output")
            return [None,None]

        # Collect all individual errors for all repeats
        got_to_section = False
        error_counts = {}
        for line in output.splitlines():
            if not got_to_section and "FINAL RESULTS PER REPETITION" not in line:
                continue
            if "FINAL RESULTS PER REPETITION" in line:
                got_to_section = True
                continue
            [achieved_error, repeat_count] = line.rstrip().split(" ")
            error_counts[float(achieved_error)] = int(repeat_count)

        return [avg_error, error_counts]


    finally:
        # Clean up temp file
        os.unlink(tmp_file)

def plot_results(all_results, output_file="delta_t_analysis.png"):
    """Create visualization of delta_t analysis"""

    fig, ax1 = plt.subplots(figsize=(14, 7))

    configs = {
        'default': {
            'color_u': 'tab:red',
            'color_l': 'tab:blue',
            'label_prefix': '',
            'sim_color': 'purple'
        },
        'opt_exec': {
            'color_u': 'indianred',
            'color_l': 'lightblue',
            'label_prefix': 'Opt Exec',
            'sim_color': 'orchid'
        },
        'opt_sched': {
            'color_u': 'darkred',
            'color_l': 'darkblue',
            'label_prefix': 'Opt Sched',
            'sim_color': 'mediumorchid'
        },
        'opt_both': {
            'color_u': 'orangered',
            'color_l': 'steelblue',
            'label_prefix': 'Opt Both',
            'sim_color': 'darkviolet'
        }
    }

    ax1.set_xlabel(r'Discrete time-step $\delta$ (sec)', fontsize=14)
    ax1.set_ylabel('Expected Error', color='k', fontsize=14)

    all_lines = []

    for config_key, result in all_results.items():
        if result is None:
            continue

        data = result['data']
        sim_results = result['sim_results']
        cfg = configs[config_key]

        delta_t = np.array(data["delta_t"])
        upper_bound = np.array(data["upper_bound"])
        lower_bound = np.array(data["lower_bound"])

        # Upper bound line
        line_u = ax1.plot(
            delta_t, upper_bound,
            color=cfg['color_u'],
            linewidth=4,
            marker='o',
            label=f"{cfg['label_prefix']} Upper bound",
            alpha=0.8
        )

        # Lower bound line
        line_l = ax1.plot(
            delta_t, lower_bound,
            color=cfg['color_l'],
            linewidth=4,
            marker='o',
            label=f"{cfg['label_prefix']} Lower bound",
            alpha=0.8
        )

        all_lines.extend(line_u + line_l)

        # Fill between bounds
        ax1.fill_between(
            delta_t,
            lower_bound,
            upper_bound,
            alpha=0.05,
            color=cfg['color_u']
        )

        # Simulation results (as lines instead of markers)
        if sim_results:

            confidence_interval_delta = 0
            sim_delta_t = []
            sim_avg_error = []
            for dt, [avg, error_counts] in sim_results.items():
                if avg is not None:
                    sim_delta_t.append(dt)
                    sim_avg_error.append(avg)
                    s_square = 0
                    num_repeats = 0
                    for error, count in error_counts.items():
                        s_square += count * (error - avg) * (error - avg)
                        num_repeats += count
                    s_square = s_square / (num_repeats - 1)
                    confidence_interval_delta = 1.96 * math.sqrt(s_square) / math.sqrt(num_repeats)
                    print(f"Error = {avg} +/- {confidence_interval_delta}")

            if sim_delta_t:
                line_sim = ax1.plot(
                    sim_delta_t,
                    sim_avg_error,
                    color=cfg['sim_color'],
                    linewidth=2,
                    linestyle='--',
                    label=f"{cfg['label_prefix']} Sim",
                    alpha=0.9
                )
                all_lines.extend(line_sim)

            # Confidence interval half-width
            lower = [x - confidence_interval_delta for x in sim_avg_error]
            upper = [x + confidence_interval_delta for x in sim_avg_error]
            ax1.fill_between(sim_delta_t, lower, upper, alpha=0.3, color='C0', label='Confidence interval')



    ax1.grid(True, alpha=0.3)
    ax1.tick_params(axis='y', labelsize=14)
    ax1.tick_params(axis='x', labelsize=14)

    # Computation time (no markers)
    if 'default' in all_results and all_results['default'] is not None:
        ax2 = ax1.twinx()
        comp_time = np.array(all_results['default']['data']["computation_time_ms"]) / 1000.0
        delta_t = np.array(all_results['default']['data']["delta_t"])

        color3 = 'tab:green'
        ax2.set_ylabel('Compute Time (sec)', fontsize=14)

        line_time = ax2.plot(
            delta_t,
            comp_time,
            color=color3,
            linewidth=4,
            linestyle='--',
            label='Preprocessing time',
            alpha=0.7
        )

        ax2.tick_params(axis='y', labelsize=14)
        all_lines.extend(line_time)

    labels = [l.get_label() for l in all_lines]
    ax1.legend(all_lines, labels, loc='lower left', fontsize=16, ncol=1)

#    plt.title(
#        'Delta_t Impact on Expected Error (Multiple Configurations)',
#        fontsize=14,
#        fontweight='bold',
#        pad=20
#    )

    plt.tight_layout()
    plt.savefig(output_file, dpi=300, bbox_inches='tight')
    print(f"Plot saved to {output_file}")

    return fig

def main():
    if len(sys.argv) < 5:
        print("Usage: python delta_t_evaluator.py <json_file> <min_delta_t> <max_delta_t> <step_size> [options]")
        print("\nOptions:")
        print("  --executable <path>          Path to delta_t_evaluator executable (default: ../build/delta_t_evaluator)")
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

    # Force the config to be compatible with the delta_t evaluator (1 host,
    # dynamic-only scheduling, both hacks off) rather than erroring out, and
    # use the resulting temp file for everything below. This avoids the
    # single-host-only ExecOptionDecisionNode map lookups crashing deep
    # inside the C++ evaluator.
    prepared_json_file = prepare_config_file(json_file)

    # Parse remaining arguments
    base_executable = "../build/delta_t_evaluator"
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
        data = run_evaluator(prepared_json_file, min_delta_t, max_delta_t, step_size, evaluator_exe)
        print(f"Bounds results: {data}")

        # Run simulations if requested
        sim_results = None
        if run_sims:
            if not os.path.exists(sim_exe):
                print(f"ERROR: Simulation executable not found: {sim_exe}")
                print(f"Skipping simulations for {config_name}")
            else:
                batch_size = 100000
                print(f"\n=== Running Simulations (in batches of {batch_size} repeats) ===")
                sim_results = {}
                delta_t_values = data["delta_t"]

                for dt in delta_t_values:

                    error_counts = {}
                    # Deal with repeats (too many repeats, i.e., in the millions, make things crash!)
                    num_repeats_done = 0
                    seed = 5123
                    while num_repeats_done < num_repeats:
                        num_repeats_to_do = min(num_repeats - num_repeats_done, batch_size)
                        [batch_avg_error, batch_error_counts] = run_simulation(prepared_json_file, dt, num_repeats_to_do, seed, sim_exe)
                        seed += num_repeats_to_do
                        # Accumulate into error_counts
                        for error, count in batch_error_counts.items():
                            if error not in error_counts:
                                error_counts[error] = count
                            else:
                                error_counts[error] += count
                        num_repeats_done += num_repeats_to_do

                    # Compute avg_error
                    avg_error = 0
                    num_samples = 0
                    print(error_counts)
                    for error, count in error_counts.items():
                        avg_error += count * error
                        num_samples += count
                    avg_error /= num_samples

                    sim_results[dt] = [avg_error, error_counts]

                print("\nSimulation results:")
                for dt, [err, errors] in sim_results.items():
                    if err is not None:
                        print(f"  delta_t={dt}: avg_error={err:.3f}")

        all_results[config_name] = {
            'data': data,
            'sim_results': sim_results
        }

    # Create visualization
    plot_results(all_results)
    print(all_results)

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

    os.unlink(prepared_json_file)

if __name__ == "__main__":
    main()
