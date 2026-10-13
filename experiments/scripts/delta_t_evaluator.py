#!/usr/bin/env python3

import argparse
import json
import subprocess
import sys
import matplotlib.pyplot as plt
import numpy as np
import re
import tempfile
import os
import math

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


def parse_args(argv=None):
    """Parse command-line arguments without running the evaluator."""
    parser = argparse.ArgumentParser(
        description="Compute delta_t bounds and optionally run simulations.",
        epilog=(
            ""
        ),
        allow_abbrev=False,
    )
    required = parser.add_argument_group("required arguments")
    required.add_argument(
        "--json-file", required=True, metavar="PATH",
        help="Path to the input JSON configuration file.",
    )
    required.add_argument(
        "--output-json", required=True, metavar="PATH",
        help="Path to the output JSON file (overwritten if it already exists).",
    )
    required.add_argument(
        "--min-delta-t", required=True, type=float, metavar="FLOAT",
        help="Minimum delta_t value.",
    )
    required.add_argument(
        "--max-delta-t", required=True, type=float, metavar="FLOAT",
        help="Maximum delta_t value.",
    )
    required.add_argument(
        "--step-size", required=True, type=float, metavar="FLOAT",
        help="Step size between delta_t values.",
    )
    parser.add_argument(
        "--executable", default="../build/delta_t_evaluator", metavar="PATH",
        help="Path to the delta_t evaluator executable (default: %(default)s).",
    )
    parser.add_argument(
        "--sim-executable", default="../build/redwood_sim", metavar="PATH",
        help="Path to the simulation executable (default: %(default)s).",
    )
    parser.add_argument(
        "--run-sims", action="store_true",
        help="Run actual simulations in addition to computing bounds.",
    )
    parser.add_argument(
        "--num-repeats", type=int, default=1000, metavar="N",
        help="Number of simulation repeats (default: %(default)s).",
    )
    return parser.parse_args(argv)


def main():
    args = parse_args()
    json_file = args.json_file
    output_json = args.output_json
    min_delta_t = args.min_delta_t
    max_delta_t = args.max_delta_t
    step_size = args.step_size
    evaluator_executable = args.executable
    sim_executable = args.sim_executable
    run_sims = args.run_sims
    num_repeats = args.num_repeats

    # Force the config to be compatible with the delta_t evaluator (1 host,
    # dynamic-only scheduling, both hacks off) rather than erroring out, and
    # use the resulting temp file for everything below. This avoids the
    # single-host-only ExecOptionDecisionNode map lookups crashing deep
    # inside the C++ evaluator.
    prepared_json_file = prepare_config_file(json_file)


    all_results = {}

    print(f"\n{'='*60}")
    print(f"{'='*60}")

    # Check if executables exist
    if not os.path.exists(evaluator_executable):
        print(f"ERROR: Executable not found: {evaluator_executable}")
        print(f"Please build with: make")
        sys.exit(1)

    # Run evaluator
    data = run_evaluator(prepared_json_file, min_delta_t, max_delta_t, step_size, evaluator_executable)

    # Run simulations if requested
    sim_results = None
    if run_sims:
        if not os.path.exists(sim_executable):
            print(f"ERROR: Simulation executable not found: {sim_executable}")
            print("Skipping simulations")
        else:
            batch_size = 100000
            print(f"\n=== Running Simulations (in batches of {batch_size} repeats) ===")
            sim_results = {}
            delta_t_values = data["delta_t"]

            for dt in delta_t_values:

                error_counts = {}
                # Deal with repeats (too many repeats, i.e., in the millions, make things crash!)
                num_repeats_done = 0
                seed = 31212342
                running_sum = 0
                while num_repeats_done < num_repeats:
                    num_repeats_to_do = min(num_repeats - num_repeats_done, batch_size)
                    [batch_avg_error, batch_error_counts] = run_simulation(prepared_json_file, dt, num_repeats_to_do, seed, sim_executable)
                    seed += num_repeats_to_do
                    num_repeats_done += num_repeats_to_do
                    running_sum += batch_avg_error * num_repeats_to_do
                    print(f"Average error after {num_repeats_done  } repeats: {running_sum / (num_repeats_done)}")
                    # Accumulate into error_counts
                    for error, count in batch_error_counts.items():
                        if error not in error_counts:
                            error_counts[error] = count
                        else:
                            error_counts[error] += count


                # Compute avg_error from scratch
                avg_error = 0
                num_samples = 0
                for error, count in error_counts.items():
                    avg_error += count * error
                    num_samples += count
                avg_error /= num_samples

                sim_results[dt] = [avg_error, error_counts]

            print("\nSimulation results:")
            for dt, [err, errors] in sim_results.items():
                if err is not None:
                    print(f"  delta_t={dt}: avg_error={err:.3f}")

    results = {
        'data': data,
        'sim_results': sim_results
    }

    # Save raw data
    output_data = {}
    output_data['data'] = results['data'].copy()
    if results['sim_results']:
        output_data['simulation_avg_error'] = [
            results['sim_results'].get(dt) for dt in results['data']["delta_t"]
        ]

    with open(output_json, "w", encoding="utf-8") as f:
        json.dump(output_data, f, indent=2)
    print(f"\nJSON results saved to {output_json}")

    os.unlink(prepared_json_file)

if __name__ == "__main__":
    main()
