#!/usr/bin/env python3
import numpy as np
import matplotlib.pyplot as plt
import json
import sys
import bisect
from matplotlib.patches import Rectangle
from matplotlib.patches import Patch

expected_error_thresholds = [1.5, 2.0, 3.0, 4.0, 5.0, 10]
colors = plt.cm.viridis(np.linspace(0, 0.9, len(expected_error_thresholds)))


def plot_heatmap(data):
    def plot_rectangle(ax: plt.figure, x: int, y: int, expected_error: float):
        index = bisect.bisect_left(expected_error_thresholds, expected_error)
        ax.add_patch(Rectangle((x, y), 1, 1, facecolor=colors[index], linewidth=0.5, edgecolor='white'))

    max_num_procs = len(data[list(data.keys())[0]])

    # Paint tiles
    fig, ax = plt.subplots()
    for deadline_idx, deadline in enumerate(data):
        for num_procs in range(1, len(data[deadline]) + 1):
            expected_error = data[deadline][num_procs - 1]
            plot_rectangle(ax, deadline_idx + 1, num_procs - 0.5, expected_error)

    ax.set_xlabel('deadline (sec)', fontsize=12)
    ax.set_ylabel('number of PEs', fontsize=12)
    ax.set_title('Near-optimal expected error', fontsize=14)

    ax.set_yticks(np.arange(1, max_num_procs + 1))
    ax.set_yticks(np.arange(1.5, max_num_procs), minor=True)
    ax.tick_params(axis='y', which='minor', length=0)  # Adjust length as needed
    ax.tick_params(axis='y', which='major', length=0)  # Hide major ticks if desired

    ax.set_xticks(np.arange(1.5, len(data) + 1))  # Set major tick positions
    ax.set_xticklabels([int(x) for x in data.keys()])  # Set labels for those positions
    ax.set_xticks(np.arange(1.5, len(data) + 1), minor=True)  # Minor ticks between
    ax.tick_params(axis='x', which='minor', length=0)  # Adjust length as needed
    ax.tick_params(axis='x', which='major', length=0)  # Hide major ticks if desired

    ax.set_xlim(1, len(data.keys()))
    ax.set_ylim(0.5, max_num_procs + 0.5)

    # Create legend elements
    legend_elements = []
    for i, color in enumerate(colors):
        if i == 0:
            label = f"≤ {expected_error_thresholds[0]}"
        elif i < len(expected_error_thresholds):
            label = f"≤ {expected_error_thresholds[i]}"
        else:
            label = f"> {expected_error_thresholds[-1]}"

        legend_elements.append(Patch(facecolor=color, label=label))

    # Add legend to the plot
    ax.legend(handles=legend_elements, handlelength=1.0, bbox_to_anchor=(1.20, 0.5), loc="center right")

    plt.tight_layout()
    sys.stderr.write("Figure saved in ./heatmap.pdf\n")
    plt.savefig("./heatmap.pdf")

def plot_curves(data):
    max_num_procs = len(data[list(data.keys())[0]])

    # Paint tiles
    fig, ax = plt.subplots()
    ax.set_xlabel('Number of PEs', fontsize=12)
    ax.set_ylabel('Near-optimal expected error', fontsize=12)
    # ax.set_title('Near-optimal expected error', fontsize=14)
    ax.grid(True)

    max_min_error = 0.0
    for deadline in data.keys():
        ax.plot(range(1, len(data[deadline]) + 1), data[deadline], label="deadline (sec):" + str(deadline))
        max_min_error = max(min(data[deadline]), max_min_error)

    ax.set_xticks(range(1, max_num_procs+1))  # Set major tick positions

    # Compute ok limits
    ax.set_ylim(1.0, max_min_error + 0.5)


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

    plot_heatmap(data)
    plot_curves(data)


if __name__ == '__main__':
    main()

