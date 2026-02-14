#!/usr/bin/env python3
import numpy as np
import matplotlib.pyplot as plt
import json
import sys
import bisect
from matplotlib.patches import Rectangle
from matplotlib.patches import Patch

def plot_curves(data):
    max_num_procs = len(data[list(data.keys())[0]])
    colors = plt.cm.viridis(np.linspace(0, 0.9, len(data.keys())))


    # Paint tiles
    fig, ax = plt.subplots()
    ax.set_xlabel('Number of PEs', fontsize=12)
    ax.set_ylabel('Near-optimal expected error', fontsize=12)
    # ax.set_title('Near-optimal expected error', fontsize=14)
    ax.grid(True)

    max_min_error = 0.0
    color_idx = 0
    for deadline in data.keys():
        color = colors[color_idx]
        num_nodes_values = [x["num_nodes"] for x in data[deadline]]
        error_values = [x["expected_error"] for x in data[deadline]]
        ax.plot(num_nodes_values, error_values, '.-', label="deadline:" + str(int(deadline)) + "s", color=color, linewidth=2)
        min_error = min(error_values)
        best_num_procs = num_nodes_values[error_values.index(min_error)]
        ax.plot([best_num_procs], [min_error], 'o', color=color, markersize=6)
        max_min_error = max(min_error, max_min_error)
        color_idx += 1

    ax.set_xticks(num_nodes_values)  # Set major tick positions

    # Compute ok limits
#    ax.set_ylim(1.0, max_min_error + 0.2)

    ax.legend()
    plt.tight_layout()
    sys.stderr.write("Figure saved in ./curves.pdf\n")
    plt.savefig("./curves.pdf")

def main():
    # Read JSON from stdin or file
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'r') as f:
            json_object = json.load(f)
    else:
        json_object = json.load(sys.stdin)

    data = {}
    for key, values in json_object.items():
        data[float(key)] = values
    print(data)

    plot_curves(data)


if __name__ == '__main__':
    main()

