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

def plot_results(all_results, output_file="delta_t_analysis.pdf"):
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

    for config_key, data in all_results.items():
        if data is None:
            continue
        cfg = configs[config_key]
        delta_t = np.array(data["delta_t"])
        upper_bound = np.array(data["upper_bound"])
        lower_bound = np.array(data["lower_bound"])
        sim_results = data["simulation_avg_error"]


        # Upper bjljound line
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
            sim_delta_t = delta_t
            sim_avg_error = []
            for [avg, error_counts] in sim_results:
                if avg is not None:
                    sim_avg_error.append(avg)
                    s_square = 0
                    num_repeats = 0
                    for error, count in error_counts.items():
                        s_square += count * (float(error) - avg) * (float(error) - avg)
                        num_repeats += count
                    s_square = s_square / (num_repeats - 1)
                    confidence_interval_delta = 1.96 * math.sqrt(s_square) / math.sqrt(num_repeats)
#                    print(f"Error = {avg} +/- {confidence_interval_delta}")

            line_sim = ax1.plot(
                delta_t,
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
        comp_time = np.array(all_results['default']["computation_time_ms"]) / 1000.0
        delta_t = np.array(all_results['default']["delta_t"])

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
    if len(sys.argv) != 2:
        print("Usage: delta_t_plotter.py <raw data json file>")
        sys.exit(1)

    json_file = sys.argv[1]
    with open(json_file, 'r') as f:
        all_results = json.load(f)

    plot_results(all_results)

if __name__ == "__main__":
    main()
