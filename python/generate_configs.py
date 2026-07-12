#!/usr/bin/env python3
"""
Generate random application configurations and save them to a SQLite database.
"""

import sqlite3
import json
import random
import math
import os
import argparse
from itertools import product as iterproduct

# ─── Fixed simulation parameters ──────────────────────────────────────────────
SEED_MIN         = 0
SEED_MAX         = 2**31 - 1
LAMBDA           = 0.001
DELTA_T          = 5.0
DEADLINE         = 10000
RESTART_OVERHEAD = 0
INITIAL_ERROR    = 1.0
NUM_REPEATS      = 25
TARGET_CONFIGS   = 10

P_MIN_SINGLE = 0.15
P_MAX_SINGLE = 0.85

NUM_NODES_OPTIONS   = [2, 4, 8, 16]
E_FAIL_MULTIPLIERS  = [2, 5]
REPETITIONS_PER_ROW = 10

# ─── Heuristics ───────────────────────────────────────────────────────────────
ALGORITHMS = [
    "dynamic",
    "random",
    "static_foresighted_expected_error",
    "static_foresighted_error_level",
    "static_foresighted_probability_success",
    "static_foresighted_success_error_ratio",
    "static_nearsighted_expected_error",
    "static_nearsighted_error_level",
    "static_nearsighted_probability_success",
    "static_nearsighted_success_error_ratio",
    "greedy_foresighted_incrementing_expected_error",
    "greedy_foresighted_incrementing_error_level",
    "greedy_foresighted_incrementing_probability_success",
    "greedy_foresighted_incrementing_success_error_ratio",
    "greedy_foresighted_decrementing_expected_error",
    "greedy_foresighted_decrementing_error_level",
    "greedy_foresighted_decrementing_probability_success",
    "greedy_foresighted_decrementing_success_error_ratio",
    "greedy_nearsighted_incrementing_expected_error",
    "greedy_nearsighted_incrementing_error_level",
    "greedy_nearsighted_incrementing_probability_success",
    "greedy_nearsighted_incrementing_success_error_ratio",
    "greedy_nearsighted_decrementing_expected_error",
    "greedy_nearsighted_decrementing_error_level",
    "greedy_nearsighted_decrementing_probability_success",
    "greedy_nearsighted_decrementing_success_error_ratio",
]
TEMPORAL_REDUNDANCY_OPTIONS = ["off", "independent", "dependent", "aggressive"]
STOP_RUNNING_JOBS_OPTIONS   = ["off", "variant", "aggressive"]

ALL_HEURISTICS = [
    (algo, tr, srj)
    for algo in ALGORITHMS
    for tr in TEMPORAL_REDUNDANCY_OPTIONS
    for srj in STOP_RUNNING_JOBS_OPTIONS
]

def heuristic_col(algo: str, tr: str, srj: str) -> str:
    return f"{algo}__{tr}__{srj}"

HEURISTIC_COLS = [heuristic_col(*h) for h in ALL_HEURISTICS]

# ─── Probability helpers ───────────────────────────────────────────────────────

def p_single_no_restart(exec_time_seconds: float) -> float:
    return math.exp(-LAMBDA * exec_time_seconds)

def p_chain_no_restart(exec_times: list) -> float:
    return p_single_no_restart(sum(exec_times))

# ─── Config generation ────────────────────────────────────────────────────────

def make_affine(a: float, b: float, c: float) -> dict:
    return {
        "type": "affine",
        "comments": "a + b * x + c * y",
        "parameters": {"a": a, "b": b, "c": c}
    }

def lerp(v_risky: float, v_safe: float, t: float) -> float:
    return v_risky + t * (v_safe - v_risky)

def generate_task(task_idx: int, num_options: int) -> tuple:
    t_risky = random.uniform(50,  600)
    t_safe  = t_risky * random.uniform(2.0, 6.0)
    e_risky = random.uniform(3.0, 10.0)
    e_safe  = random.uniform(1.0, min(e_risky * 0.6, e_risky - 0.5))
    d_risky = random.uniform(1e10, 1e12)
    d_safe  = d_risky * random.uniform(0.01, 0.3)

    options    = []
    exec_times = []

    for i in range(num_options):
        t      = 0.0 if num_options == 1 else i / (num_options - 1)
        exec_t = lerp(t_risky, t_safe, t)
        data_d = lerp(d_risky, d_safe, t)
        err_e  = lerp(e_risky, e_safe, t)

        options.append({
            "name": f"option_{i}",
            "amhdal_parallelizable_fraction": round(random.uniform(0.85, 0.99), 4),
            "t_function": make_affine(round(exec_t, 4), 0.0, 0.0),
            "d_function": make_affine(round(data_d, 2), 0.0, 0.0),
            "e_function": make_affine(0.0, 0.0, round(err_e, 6)),
        })
        exec_times.append(exec_t)

    task = {
        "name": f"task_{task_idx}",
        "execution_options": options,
        "in_situ_with_next_task": False,
    }

    print(f"    [task_{task_idx}] {num_options} options | "
          f"t=[{t_risky:.1f}..{t_safe:.1f}]s | "
          f"e=[{e_risky:.2f}..{e_safe:.2f}] | "
          f"d=[{d_risky:.2e}..{d_safe:.2e}]", flush=True)

    return task, exec_times

def compute_max_error(tasks: list) -> float:
    max_err = INITIAL_ERROR
    for task in tasks:
        task_max = max(
            opt["e_function"]["parameters"]["c"]
            for opt in task["execution_options"]
        )
        max_err *= task_max
    return max_err

def generate_config(attempt: int) -> tuple | None:
    num_tasks        = random.randint(2, 5)
    num_options_list = [random.randint(2, 4) for _ in range(num_tasks)]

    print(f"  Attempt {attempt}: trying {num_tasks} tasks, "
          f"options={num_options_list}", flush=True)

    tasks              = []
    exec_times_by_task = []

    for i in range(num_tasks):
        task, exec_times = generate_task(i, num_options_list[i])
        tasks.append(task)
        exec_times_by_task.append(exec_times)

    fastest_total = sum(times[0]  for times in exec_times_by_task)
    slowest_total = sum(times[-1] for times in exec_times_by_task)
    p_fastest     = p_chain_no_restart([fastest_total])
    p_slowest     = p_chain_no_restart([slowest_total])

    print(f"    fastest combo exec_time={fastest_total:.1f}s -> p={p_fastest:.4f}", flush=True)
    print(f"    slowest combo exec_time={slowest_total:.1f}s -> p={p_slowest:.4f}", flush=True)

    if not (P_MIN_SINGLE <= p_fastest <= P_MAX_SINGLE):
        print(f"    REJECTED: p_fastest={p_fastest:.4f} not in "
              f"[{P_MIN_SINGLE}, {P_MAX_SINGLE}]", flush=True)
        return None
    if p_slowest < 0.01:
        print(f"    REJECTED: p_slowest={p_slowest:.4f} < 0.01 (too hard)", flush=True)
        return None

    max_error = compute_max_error(tasks)
    print(f"    ACCEPTED: max_error={max_error:.4f}", flush=True)

    seed = random.randint(SEED_MIN, SEED_MAX)

    config = {
        "platform": {
            "num_compute_nodes": 2,
            "io_read_bandwidth_per_node": "2GBps",
            "io_write_bandwidth_per_node": "1GBps",
        },
        "failures": {
            "restart_overhead": RESTART_OVERHEAD,
            "lambda": LAMBDA,
            "seed": seed,
        },
        "application": {
            "initial_data_size": round(random.uniform(1e6, 1e10), 2),
            "initial_error_level": INITIAL_ERROR,
            "tasks": tasks,
        },
        "execution": {
            "num_repeats": NUM_REPEATS,
            "deadline": DEADLINE,
            "e_fail": None,
        },
        "scheduling": {
            "delta_t_scheme": {"scheme": "fixed", "parameter": DELTA_T},
            "algorithms": [],
            "hacks": {
                "temporal_redundancy": "off",
                "stop_running_jobs":   "off",
            },
        },
    }

    metadata = {
        "num_tasks":        num_tasks,
        "num_options_list": num_options_list,
        "p_fastest_single": p_fastest,
        "p_slowest_single": p_slowest,
        "max_error":        max_error,
    }

    return config, metadata

# ─── Database ─────────────────────────────────────────────────────────────────

def create_database(db_path: str) -> sqlite3.Connection:
    print(f"\nCreating database at {os.path.abspath(db_path)}", flush=True)
    conn = sqlite3.connect(db_path)
    c    = conn.cursor()

    c.execute("""
              CREATE TABLE IF NOT EXISTS app_configs (
                                                         app_config_id        INTEGER PRIMARY KEY AUTOINCREMENT,
                                                         num_tasks            INTEGER NOT NULL,
                                                         num_options_per_task TEXT    NOT NULL,
                                                         p_fastest_single     REAL    NOT NULL,
                                                         p_slowest_single     REAL    NOT NULL,
                                                         max_error            REAL    NOT NULL,
                                                         config_json          TEXT    NOT NULL
              )
              """)
    print("  Created table: app_configs", flush=True)

    heuristic_col_defs = "\n".join(f"    {col} REAL," for col in HEURISTIC_COLS)
    heuristic_col_defs = heuristic_col_defs.rstrip(",\n ") + "\n"

    c.execute(f"""
        CREATE TABLE IF NOT EXISTS results (
            row_id              INTEGER PRIMARY KEY AUTOINCREMENT,
            repetition_id       INTEGER NOT NULL,
            app_config_id       INTEGER NOT NULL,
            num_nodes           INTEGER NOT NULL,
            e_fail_multiplier   REAL    NOT NULL,
            e_fail              REAL    NOT NULL,
            lambda              REAL    NOT NULL,
            restart_overhead    REAL    NOT NULL,
            deadline            REAL    NOT NULL,
            delta_t             REAL    NOT NULL,
            {heuristic_col_defs},
            FOREIGN KEY (app_config_id) REFERENCES app_configs(app_config_id)
        )
    """)
    print(f"  Created table: results ({len(HEURISTIC_COLS)} heuristic columns)", flush=True)

    conn.commit()
    return conn

def insert_app_config(conn: sqlite3.Connection, config: dict, metadata: dict) -> int:
    c = conn.cursor()
    c.execute("""
              INSERT INTO app_configs (
                  num_tasks, num_options_per_task,
                  p_fastest_single, p_slowest_single,
                  max_error, config_json
              ) VALUES (?, ?, ?, ?, ?, ?)
              """, (
                  metadata["num_tasks"],
                  json.dumps(metadata["num_options_list"]),
                  metadata["p_fastest_single"],
                  metadata["p_slowest_single"],
                  metadata["max_error"],
                  json.dumps(config, indent=2),
              ))
    conn.commit()
    return c.lastrowid

def insert_result_rows(conn: sqlite3.Connection, app_config_id: int, max_error: float):
    c          = conn.cursor()
    col_names  = ", ".join(HEURISTIC_COLS)
    null_vals  = ", ".join(["NULL"] * len(HEURISTIC_COLS))
    placeholder = ", ".join(["?"] * 9)

    rows = []
    for rep_id in range(REPETITIONS_PER_ROW):
        for num_nodes in NUM_NODES_OPTIONS:
            for e_mult in E_FAIL_MULTIPLIERS:
                e_fail = round(max_error * e_mult, 4)
                rows.append((
                    rep_id, app_config_id, num_nodes,
                    e_mult, e_fail,
                    LAMBDA, RESTART_OVERHEAD, DEADLINE, DELTA_T,
                ))

    c.executemany(f"""
        INSERT INTO results (
            repetition_id, app_config_id, num_nodes,
            e_fail_multiplier, e_fail, lambda,
            restart_overhead, deadline, delta_t,
            {col_names}
        ) VALUES ({placeholder}, {null_vals})
    """, rows)

    conn.commit()
    print(f"  Inserted {len(rows)} result rows for app_config_id={app_config_id}", flush=True)

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Generate Redwood app configs.")
    parser.add_argument(
        "--reset",
        action="store_true",
        help="Delete the existing database and start fresh. "
             "If omitted, new configs are appended to the existing database.",
    )
    args = parser.parse_args()

    db_path = "../database/configs.db"

    if args.reset and os.path.exists(db_path):
        print(f"--reset passed: removing existing database: {db_path}", flush=True)
        os.remove(db_path)
    elif os.path.exists(db_path):
        print(f"Appending to existing database: {db_path}", flush=True)
    else:
        print(f"No existing database found. Creating new one: {db_path}", flush=True)

    db_dir = os.path.dirname(db_path)
    if db_dir and not os.path.exists(db_dir):
        os.makedirs(db_dir, exist_ok=True)
        print(f"  Created missing directory: {db_dir}", flush=True)

    conn = create_database(db_path)

    rows_per_config = REPETITIONS_PER_ROW * len(NUM_NODES_OPTIONS) * len(E_FAIL_MULTIPLIERS)

    print(f"\n{'='*60}")
    print(f"Generating {TARGET_CONFIGS} app configs")
    print(f"  lambda             : {LAMBDA}/s  (mean TTF = {1/LAMBDA:.0f}s)")
    print(f"  deadline           : {DEADLINE}s")
    print(f"  single-node P range: [{P_MIN_SINGLE}, {P_MAX_SINGLE}]")
    print(f"  num_nodes options  : {NUM_NODES_OPTIONS}")
    print(f"  e_fail multipliers : {E_FAIL_MULTIPLIERS}")
    print(f"  repetitions/combo  : {REPETITIONS_PER_ROW}")
    print(f"  heuristics         : {len(ALL_HEURISTICS)}")
    print(f"  rows per config    : {rows_per_config}")
    print(f"  total result rows  : {TARGET_CONFIGS * rows_per_config}")
    print(f"{'='*60}\n")

    generated    = 0
    attempts     = 0
    max_attempts = TARGET_CONFIGS * 500

    while generated < TARGET_CONFIGS and attempts < max_attempts:
        attempts += 1
        print(f"\n--- Config {generated+1}/{TARGET_CONFIGS} ---", flush=True)
        result = generate_config(attempts)
        if result is None:
            continue

        config, metadata = result
        app_config_id = insert_app_config(conn, config, metadata)
        insert_result_rows(conn, app_config_id, metadata["max_error"])
        generated += 1

        print(f"  -> Saved as app_config_id={app_config_id}  "
              f"tasks={metadata['num_tasks']}  "
              f"options={metadata['num_options_list']}  "
              f"p_single=[{metadata['p_slowest_single']:.3f}, {metadata['p_fastest_single']:.3f}]  "
              f"max_error={metadata['max_error']:.2f}", flush=True)

    print(f"\n{'='*60}")
    if generated < TARGET_CONFIGS:
        print(f"WARNING: only {generated}/{TARGET_CONFIGS} generated after {attempts} attempts.")
    else:
        print(f"Done. Generated {generated} configs in {attempts} attempts.")

    print(f"\nDatabase : {os.path.abspath(db_path)}")
    print(f"app_configs rows : {generated}")
    print(f"results rows     : {generated * rows_per_config}")
    print(f"{'='*60}")

    conn.close()

if __name__ == "__main__":
    main()