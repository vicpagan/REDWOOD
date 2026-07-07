#!/usr/bin/env python3

import copy
import csv
import itertools
import json
import pathlib
import subprocess
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime

# ============================================================================
# USER SETTINGS
# ============================================================================

BASE_CONFIG = "../data/fusion_science_use_case.json"

SIMULATOR_CMD = [
    "../build/redwood_sim_opt_both",
    "--json"
]

NUM_REPEATS = 15

# Number of configurations to run at the same time.
# Rule of thumb: start with os.cpu_count(), then tune based on how
# CPU/memory-heavy a single simulator run is.
MAX_WORKERS = 4

# ============================================================================
# ALGORITHMS
# ============================================================================

COMPARATORS = [
    "expected_error",
    "error_level",
    "probability_success",
    "success_error_ratio",
]

ALGORITHMS = [
    "dynamic",
    "random",
]

for base in [
    "static_foresighted",
    "static_nearsighted",
]:
    for comp in COMPARATORS:
        ALGORITHMS.append(f"{base}_{comp}")

for base in [
    "greedy_foresighted_incrementing",
    "greedy_foresighted_decrementing",
    "greedy_nearsighted_incrementing",
    "greedy_nearsighted_decrementing",
]:
    for comp in COMPARATORS:
        ALGORITHMS.append(f"{base}_{comp}")

TEMPORAL_REDUNDANCY_OPTIONS = [
    "off",
    "independent",
    "dependent",
    "aggressive",
]

STOP_RUNNING_JOBS_OPTIONS = [
    "off",
    "variant",
    "aggressive",
]

EXPECTED_CONFIGS = (
        len(ALGORITHMS)
        * len(TEMPORAL_REDUNDANCY_OPTIONS)
        * len(STOP_RUNNING_JOBS_OPTIONS)
)

# ============================================================================
# OUTPUT DIRECTORIES
# ============================================================================

timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

ROOT_DIR = pathlib.Path(f"scheduler_sweep_{timestamp}")
CONFIG_DIR = ROOT_DIR / "configs"
LOG_DIR = ROOT_DIR / "logs"

CONFIG_DIR.mkdir(parents=True, exist_ok=True)
LOG_DIR.mkdir(parents=True, exist_ok=True)

# ============================================================================
# LOAD BASE CONFIG
# ============================================================================

with open(BASE_CONFIG, "r") as f:
    base_cfg = json.load(f)

base_cfg["execution"]["num_repeats"] = NUM_REPEATS

# ============================================================================
# SUMMARY FILE
# ============================================================================

summary_csv = ROOT_DIR / "summary.csv"

# Guards prints and the shared counter so parallel workers don't
# interleave/garble console output or step on each other's increments.
print_lock = threading.Lock()
counter_lock = threading.Lock()
config_counter = 0


def run_one_config(job_index, algorithm, temporal_redundancy, stop_running_jobs):
    """Build the config, run the simulator, write its log, and return a summary row.

    Runs in a worker thread -- must not mutate shared state except through
    the locks above, and must not rely on ordering relative to other jobs.
    """
    global config_counter

    cfg = copy.deepcopy(base_cfg)

    cfg["scheduling"]["algorithms"] = [algorithm]
    cfg["scheduling"]["hacks"]["temporal_redundancy"] = temporal_redundancy
    cfg["scheduling"]["hacks"]["stop_running_jobs"] = stop_running_jobs

    config_name = (
        f"{algorithm}"
        f"__tr-{temporal_redundancy}"
        f"__srj-{stop_running_jobs}"
    )

    config_file = CONFIG_DIR / f"{config_name}.json"
    log_file = LOG_DIR / f"{config_name}.log"

    with open(config_file, "w") as f:
        json.dump(cfg, f, indent=2)

    with counter_lock:
        config_counter += 1
        current = config_counter

    with print_lock:
        print(f"[{current:03d}/{EXPECTED_CONFIGS}] START  {config_name}")

    start_time = time.time()

    with open(log_file, "w") as log:

        log.write(f"Configuration: {config_name}\n")
        log.write(f"Started: {datetime.now()}\n")
        log.write("=" * 80 + "\n\n")

        try:
            subprocess.run(
                SIMULATOR_CMD + [str(config_file)],
                stdout=log,
                stderr=subprocess.STDOUT,
                text=True,
                check=True,
                )

            status = "SUCCESS"
            return_code = 0

        except subprocess.CalledProcessError as e:

            status = "FAILED"
            return_code = e.returncode

            log.write("\n")
            log.write("=" * 80 + "\n")
            log.write("PROCESS FAILED\n")
            log.write(f"Return code: {e.returncode}\n")

        except Exception as e:

            status = "ERROR"
            return_code = -1

            log.write("\n")
            log.write("=" * 80 + "\n")
            log.write("UNEXPECTED ERROR\n")
            log.write(str(e))
            log.write("\n")

    runtime = time.time() - start_time

    with print_lock:
        print(f"[{current:03d}/{EXPECTED_CONFIGS}] {status:7s} {config_name} ({runtime:.2f}s)")

    return [
        config_name,
        algorithm,
        temporal_redundancy,
        stop_running_jobs,
        status,
        return_code,
        f"{runtime:.2f}",
    ]


# ============================================================================
# MAIN LOOP
# ============================================================================

print(f"Running {EXPECTED_CONFIGS} scheduler configurations")
print(f"Each configuration will use {NUM_REPEATS} repetitions")
print(f"Running with up to {MAX_WORKERS} configurations in parallel")
print()

summary_rows = []

all_combos = list(itertools.product(
    ALGORITHMS,
    TEMPORAL_REDUNDANCY_OPTIONS,
    STOP_RUNNING_JOBS_OPTIONS,
))

with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
    futures = {
        executor.submit(run_one_config, idx, algorithm, temporal_redundancy, stop_running_jobs): idx
        for idx, (algorithm, temporal_redundancy, stop_running_jobs) in enumerate(all_combos)
    }

    for future in as_completed(futures):
        summary_rows.append(future.result())

# Results complete in whatever order finished first; sort by configuration
# name so the summary CSV is stable and easy to diff between runs.
summary_rows.sort(key=lambda row: row[0])

# ============================================================================
# WRITE SUMMARY CSV
# ============================================================================

with open(summary_csv, "w", newline="") as f:

    writer = csv.writer(f)

    writer.writerow([
        "configuration",
        "algorithm",
        "temporal_redundancy",
        "stop_running_jobs",
        "status",
        "return_code",
        "runtime_seconds",
    ])

    writer.writerows(summary_rows)

# ============================================================================
# FINAL REPORT
# ============================================================================

successes = sum(
    1 for row in summary_rows
    if row[4] == "SUCCESS"
)

failures = len(summary_rows) - successes

print("\n" + "=" * 80)
print("RUN COMPLETE")
print("=" * 80)

print(f"Configurations attempted : {len(summary_rows)}")
print(f"Successful              : {successes}")
print(f"Failed                  : {failures}")

if failures:

    print("\nFailed configurations:")

    for row in summary_rows:
        if row[4] != "SUCCESS":
            print(
                f"  {row[0]} "
                f"(status={row[4]}, rc={row[5]})"
            )

print(f"\nSummary CSV:")
print(f"  {summary_csv}")

print(f"\nLogs:")
print(f"  {LOG_DIR}")

print(f"\nConfigs:")
print(f"  {CONFIG_DIR}")