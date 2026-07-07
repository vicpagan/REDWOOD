#!/usr/bin/env python3
"""
Runner script for redwood simulator.

For each (app_config, num_nodes, e_fail_multiplier) combo:
  - Ensures result rows exist in the DB for each repetition
  - For each heuristic column that is NULL, runs the simulator and fills it in
  - Skips any heuristic that already has a result
  - Runs combos in parallel, each process handles one (app_config, num_nodes,
    e_fail_multiplier, heuristic) at a time to avoid write conflicts
"""

import sqlite3
import json
import subprocess
import tempfile
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

# ─── Config ───────────────────────────────────────────────────────────────────
DB_PATH        = "configurations.db"
SIM_EXECUTABLE = "../build/redwood_sim_opt_both"

ALGORITHMS = [
    "dynamic",
    "random",
    "static_nearsighted_error_level",
    "static_nearsighted_expected_error",
    "static_nearsighted_probability_success",
    "static_nearsighted_success_error_ratio",
    "static_foresighted_error_level",
    "static_foresighted_expected_error",
    "static_foresighted_probability_success",
    "static_foresighted_success_error_ratio",
    "greedy_foresighted_decrementing_expected_error",
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

# ─── DB helpers ───────────────────────────────────────────────────────────────

def get_connection(db_path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(db_path, timeout=30)
    conn.execute("PRAGMA journal_mode=WAL")  # allows concurrent reads during writes
    return conn


def ensure_result_rows(conn: sqlite3.Connection, app_config_id: int,
                       num_nodes: int, e_fail_multiplier: float,
                       e_fail: float, num_repetitions: int,
                       lambda_: float, restart_overhead: float,
                       deadline: float, delta_t: float):
    """
    Insert result rows for each repetition if they don't already exist.
    """
    c = conn.cursor()
    null_cols  = ", ".join(HEURISTIC_COLS)
    null_vals  = ", ".join(["NULL"] * len(HEURISTIC_COLS))

    for rep_id in range(num_repetitions):
        c.execute("""
                  SELECT row_id FROM results
                  WHERE app_config_id=? AND num_nodes=? AND e_fail_multiplier=? AND repetition_id=?
                  """, (app_config_id, num_nodes, e_fail_multiplier, rep_id))
        if c.fetchone() is None:
            c.execute(f"""
                INSERT INTO results (
                    repetition_id, app_config_id, num_nodes,
                    e_fail_multiplier, e_fail, lambda,
                    restart_overhead, deadline, delta_t,
                    {null_cols}
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, {null_vals})
            """, (rep_id, app_config_id, num_nodes, e_fail_multiplier,
                  e_fail, lambda_, restart_overhead, deadline, delta_t))
    conn.commit()


def get_null_heuristics(conn: sqlite3.Connection, app_config_id: int,
                        num_nodes: int, e_fail_multiplier: float) -> list:
    """
    Return list of heuristic column names that still have NULL results
    for ANY repetition of this combo. Only need to run heuristics with nulls.
    """
    c = conn.cursor()
    null_heuristics = []
    for col in HEURISTIC_COLS:
        c.execute(f"""
            SELECT COUNT(*) FROM results
            WHERE app_config_id=? AND num_nodes=? AND e_fail_multiplier=?
            AND {col} IS NULL
        """, (app_config_id, num_nodes, e_fail_multiplier))
        count = c.fetchone()[0]
        if count > 0:
            null_heuristics.append(col)
    return null_heuristics


def update_repetition_results(conn: sqlite3.Connection, app_config_id: int,
                              num_nodes: int, e_fail_multiplier: float,
                              col: str, results_by_rep: dict):
    """
    Update the heuristic column for each repetition row with the simulator result.
    results_by_rep: {repetition_id -> error_level}
    """
    c = conn.cursor()
    for rep_id, error_val in results_by_rep.items():
        c.execute(f"""
            UPDATE results SET {col} = ?
            WHERE app_config_id=? AND num_nodes=? AND e_fail_multiplier=? AND repetition_id=?
        """, (error_val, app_config_id, num_nodes, e_fail_multiplier, rep_id))
    conn.commit()


# ─── Simulator ────────────────────────────────────────────────────────────────

def parse_repetition_results(output: str, num_repetitions: int) -> dict | None:
    """
    Parse the FINAL RESULTS PER REPETITION block from simulator output.
    Returns {rep_id -> error_level} or None if parsing fails.
    """
    results = {}
    for i in range(num_repetitions):
        match = re.search(rf'Repetition\s+{i}\s*:\s*([0-9.eE+\-]+)', output)
        if match:
            results[i] = float(match.group(1))
        else:
            return None
    return results


def build_sim_config(base_config: dict, num_nodes: int, e_fail: float,
                     algo: str, tr: str, srj: str, num_repetitions: int) -> dict:
    """Build a complete simulator config for one heuristic run."""
    config = json.loads(json.dumps(base_config))  # deep copy
    config["platform"]["num_compute_nodes"] = num_nodes
    config["execution"]["e_fail"]           = e_fail
    config["execution"]["num_repeats"]      = num_repetitions
    config["scheduling"]["algorithms"]      = [algo]
    config["scheduling"]["hacks"]["temporal_redundancy"] = tr
    config["scheduling"]["hacks"]["stop_running_jobs"]   = srj
    return config


def run_heuristic(args: tuple) -> tuple:
    """
    Run the simulator for one (app_config_id, num_nodes, e_fail_multiplier, heuristic).
    Returns (app_config_id, num_nodes, e_fail_multiplier, col, results_by_rep, error_msg).
    """
    (db_path, app_config_id, base_config_json, num_nodes, e_fail_multiplier,
     e_fail, algo, tr, srj, num_repetitions, sim_executable) = args

    col = heuristic_col(algo, tr, srj)

    config = build_sim_config(
        json.loads(base_config_json),
        num_nodes, e_fail, algo, tr, srj, num_repetitions
    )

    with tempfile.NamedTemporaryFile(mode='w', suffix='.json', delete=False) as tmp:
        json.dump(config, tmp, indent=2)
        tmp_path = tmp.name

    try:
        result = subprocess.run(
            [sim_executable, "--json", tmp_path],
            capture_output=True, text=True
        )

        if result.returncode != 0:
            return (app_config_id, num_nodes, e_fail_multiplier, col, None,
                    f"Simulator failed: {result.stderr[:200]}")

        results_by_rep = parse_repetition_results(result.stdout, num_repetitions)
        if results_by_rep is None:
            return (app_config_id, num_nodes, e_fail_multiplier, col, None,
                    f"Could not parse output:\n{result.stdout[:300]}")

        print(f"  Done: config={app_config_id} nodes={num_nodes} "
              f"e_fail_mult={e_fail_multiplier} heuristic={col}", flush=True)
        return app_config_id, num_nodes, e_fail_multiplier, col, results_by_rep, None

    finally:
        os.unlink(tmp_path)


# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    if not os.path.exists(DB_PATH):
        print(f"Error: database not found at {DB_PATH}")
        sys.exit(1)

    if not os.path.exists(SIM_EXECUTABLE):
        print(f"Error: simulator not found at {SIM_EXECUTABLE}")
        sys.exit(1)

    conn = get_connection(DB_PATH)
    c    = conn.cursor()

    # Load all app configs
    c.execute("SELECT app_config_id, config_json FROM app_configs")
    app_configs = c.fetchall()
    print(f"Loaded {len(app_configs)} app configs from {DB_PATH}")

    # Load execution parameters from first result row (same for all)
    c.execute("SELECT lambda, restart_overhead, deadline, delta_t FROM results LIMIT 1")
    row = c.fetchone()
    if row:
        lambda_, restart_overhead, deadline, delta_t = row
    else:
        # Fall back to defaults if results table is empty
        lambda_, restart_overhead, deadline, delta_t = 0.001, 0.0, 10000.0, 5.0

    # Load e_fail multipliers and num_nodes from generate script constants
    # (read them from existing rows or derive from config)
    c.execute("""
              SELECT DISTINCT num_nodes, e_fail_multiplier, e_fail
              FROM results LIMIT 1
              """)
    # Build combos from the DB itself so this script stays in sync
    c.execute("SELECT DISTINCT num_nodes FROM results ORDER BY num_nodes")
    num_nodes_options = [r[0] for r in c.fetchall()]

    c.execute("SELECT DISTINCT e_fail_multiplier, e_fail FROM results ORDER BY e_fail_multiplier")
    e_fail_combos = c.fetchall()  # [(multiplier, e_fail), ...]

    if not num_nodes_options:
        # DB has no rows yet — derive from generate script defaults
        num_nodes_options = [2, 4, 8, 16, 32]
        e_fail_combos     = []  # will be computed per config

    conn.close()

    # ── Build job list ─────────────────────────────────────────────────────────
    # Each job = one (app_config, num_nodes, e_fail_multiplier, heuristic) tuple
    # Only jobs where the heuristic column is still NULL for any repetition

    jobs = []
    conn = get_connection(DB_PATH)

    for app_config_id, config_json in app_configs:
        base_config    = json.loads(config_json)
        num_repetitions = base_config["execution"]["num_repeats"]

        for num_nodes in num_nodes_options:
            for e_mult, e_fail in e_fail_combos:
                # Ensure rows exist for all repetitions
                ensure_result_rows(
                    conn, app_config_id, num_nodes, e_mult, e_fail,
                    num_repetitions, lambda_, restart_overhead, deadline, delta_t
                )

                # Find which heuristics still need to be run
                null_cols = get_null_heuristics(conn, app_config_id, num_nodes, e_mult)

                for col in null_cols:
                    # Reverse-lookup heuristic from column name
                    algo, tr, srj = col.split("__")
                    jobs.append((
                        DB_PATH, app_config_id, config_json,
                        num_nodes, e_mult, e_fail,
                        algo, tr, srj,
                        num_repetitions, SIM_EXECUTABLE
                    ))

    conn.close()

    total = len(jobs)
    print(f"\nJobs to run: {total}")
    if total == 0:
        print("All results already filled in. Nothing to do.")
        return

    # ── Run in parallel ────────────────────────────────────────────────────────
    # Each job writes to a unique (app_config_id, num_nodes, e_fail_mult, col)
    # so there are no write conflicts between parallel processes

    completed = 0
    failed    = 0

    with ProcessPoolExecutor() as executor:
        futures = {executor.submit(run_heuristic, job): job for job in jobs}

        for future in as_completed(futures):
            app_config_id, num_nodes, e_fail_multiplier, col, results_by_rep, err = future.result()

            if err:
                print(f"  FAILED config={app_config_id} nodes={num_nodes} "
                      f"e_fail_mult={e_fail_multiplier} col={col}: {err}",
                      flush=True)
                failed += 1
                continue

            # Write results back to DB
            write_conn = get_connection(DB_PATH)
            update_repetition_results(
                write_conn, app_config_id, num_nodes,
                e_fail_multiplier, col, results_by_rep
            )
            write_conn.close()
            completed += 1

    print(f"\nFinished. Completed: {completed}  Failed: {failed}  Total: {total}")


if __name__ == "__main__":
    main()