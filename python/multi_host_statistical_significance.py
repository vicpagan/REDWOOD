import sys
import subprocess

def run_it(perc, seed):
    command_line = "python ./multi_host_result_generation.py ../experiment_results/results_multi_host_new.csv.gz  --exclude-heuristic random --exclude-heuristic greedy --exclude-heuristic dynamic  --show-ranking --include-algorithm dynamic__dependent__aggressive --include-algorithm greedy_foresighted_expected_error__dependent__off --include-algorithm greedy_foresighted_success_error_ratio__dependent__aggressive "
    command_line += f"--shuffle-rows {seed} "
    command_line += f"--head {perc} "

    result = subprocess.run(
        command_line.split(),
        capture_output=True,
        text=True,
        check=True,
    )

    results = {}
    lines = result.stdout.splitlines()
    for idx in range(0, len(lines)):
        if not "nodes:" in lines[idx]:
            continue
        num_nodes = lines[idx].split(" ")[3]
        results[num_nodes] = []
        for offset in [1,2,3]:
            algorithm_name = lines[idx+offset].split(" ")[4][0:-1]
            dfb_position = lines[idx+offset].find("dfb=")
            dfb = float((lines[idx+offset][dfb_position:].split("\t")[0].split("=")[1]))
            results[num_nodes].append([algorithm_name, dfb])
        idx += 3
    return results

from collections import Counter


def ranking_key(entries, tolerance=5.0):
    """
    Convert [algorithm_name, dfb] entries into ordered tie groups.

    Each group's maximum DFB minus minimum DFB is at most tolerance.
    The returned tuple is hashable and ignores ordering within a group.
    """
    if tolerance < 0:
        raise ValueError("tolerance must be nonnegative")

    tiers = []
    tier_min = None

    for name, dfb in sorted(entries, key=lambda pair: pair[1]):
        if tier_min is None or dfb - tier_min > tolerance:
            # Start a new group, anchored at its best DFB.
            tiers.append([name])
            tier_min = dfb
        else:
            tiers[-1].append(name)

    # Canonicalize the ordering within each tied group.
    return tuple(tuple(sorted(tier)) for tier in tiers)


def main(perc, num_seeds):
    data = {}
    for seed in range(0,num_seeds):
        print(f"{seed}/{num_seeds}")
        data[seed] = run_it(perc,seed)

    tolerance = 5.0  # DFB percentage points; adjust to your needs.

    # Discover the M values and algorithm names from the data.
    # As in your original code, assume all groups have the same M values
    # and contain the same algorithms.
    first_group = next(iter(data.values()))
    m_values = sorted(first_group, key=int)
    algorithm_names = [
        name for name, _ in next(iter(first_group.values()))
    ]

    labels = {
        name: chr(ord("A") + i)
        for i, name in enumerate(algorithm_names)
    }

    # counts[M][ordering] is the number of observations with that tied ranking.
    counts = {
        m: Counter(
            ranking_key(group[m], tolerance)
            for group in data.values()
        )
        for m in m_values
    }

    # Collect observed orderings, preserving first-appearance order.
    orderings = list(dict.fromkeys(
        ordering
        for m in m_values
        for ordering in counts[m]
    ))

    def format_ordering(ordering):
        parts = []
        for tier in ordering:
            names = sorted(labels[name] for name in tier)
            parts.append(
                names[0] if len(names) == 1
                else "{" + ", ".join(names) + "}"
            )
        return " > ".join(parts)

    for name, label in labels.items():
        print(f"{label} = {name}")
    print()

    width = max(
        len("Ordering"),
        *(len(format_ordering(ordering)) for ordering in orderings),
    ) + 2

    print(
        f"{'Ordering':<{width}}"
        + "".join(f"{'M=' + str(m):>8}" for m in m_values)
    )
    print("-" * (width + 8 * len(m_values)))

    # Total number of cases for each M.
    totals = {
        m: sum(counts[m].values())
        for m in m_values
    }

    for ordering in orderings:
        print(
            f"{format_ordering(ordering):<{width}}"
            + "".join(
                f"{counts[m][ordering] / totals[m]:>8.1%}"
                for m in m_values
            )
        )

if __name__ == "__main__":

    if len(sys.argv) != 3:
        sys.stderr.write(f"Usage: {sys.argv[0]} <% results to include> <num seeds to try>\n")
        sys.exit(1)
    main(int(sys.argv[1]), int(sys.argv[2]))
