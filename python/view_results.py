#!/usr/bin/env python3
"""
Interactive viewer for algorithm_comparison_results.json.
Use the checkboxes on the left to toggle individual lines on/off.
"""

import json
import sys
import os
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from matplotlib.widgets import CheckButtons
from matplotlib.lines import Line2D

if len(sys.argv) < 2:
    print("Usage: python view_results.py <algorithm_comparison_results.json>")
    sys.exit(1)

results_file = sys.argv[1]
if not os.path.exists(results_file):
    print(f"Error: File not found: {results_file}")
    sys.exit(1)

with open(results_file, 'r') as f:
    data = json.load(f)

deadlines = data['deadlines']
raw = data['results']

# ── Styling ──────────────────────────────────────────────────────────────────
comparator_color_map = {
    None:                  '#1f77b4',
    'expected_error':      '#ff7f0e',
    'probability_success': '#2ca02c',
    'error_level':         '#d62728',
    'success_error_ratio': '#9467bd',
}

algorithm_dash_map = {
    'dynamic':            '-',
    'static_foresighted': '-',
    'static_nearsighted': '--',
    'random':             ':',
}

algorithm_marker_map = {
    'dynamic':            's',
    'static_foresighted': 'o',
    'static_nearsighted': '^',
    'random':             'D',
}

# ── Group by (algorithm, comparator) ─────────────────────────────────────────
grouped = {}
for r in raw:
    key = f"{r['algorithm']}_{r['comparator']}" if r['comparator'] else r['algorithm']
    if key not in grouped:
        grouped[key] = {'name': r['algorithm'], 'comparator': r['comparator'], 'points': {}}
    grouped[key]['points'][r['deadline']] = r

keys        = list(grouped.keys())
labels      = [k.replace('_', ' ') for k in keys]
n_series    = len(keys)

# ── Layout: checkboxes on left, 3 plots on right ─────────────────────────────
fig = plt.figure(figsize=(20, 8))
fig.suptitle('Algorithm Comparison (use checkboxes to toggle lines)', fontsize=14, fontweight='bold')

# Reserve left portion for checkboxes, right for plots
gs = gridspec.GridSpec(1, 2, width_ratios=[1, 5], figure=fig)
check_ax = fig.add_subplot(gs[0])
check_ax.set_visible(False)  # hide axes frame, CheckButtons draws its own

plot_gs = gridspec.GridSpecFromSubplotSpec(1, 3, subplot_spec=gs[1], wspace=0.35)
ax1 = fig.add_subplot(plot_gs[0])
ax2 = fig.add_subplot(plot_gs[1])
ax3 = fig.add_subplot(plot_gs[2])

axes = [ax1, ax2, ax3]
titles   = ['Success Rate (%)', 'Avg Error (All Runs)', 'Avg Error (Successes Only)']
y_fields = ['success_rate_pct', 'avg_error', 'avg_error_successes']

for ax, title in zip(axes, titles):
    ax.set_title(title, fontsize=11, fontweight='bold')
    ax.set_xlabel('Deadline', fontsize=10)
    ax.grid(True, alpha=0.3)
    ax.set_xticks(deadlines)
    ax.tick_params(axis='x', rotation=45)

ax1.set_ylim(0, 100)
ax1.set_ylabel('Success Rate (%)', fontsize=10)
ax2.set_ylabel('Average Error Level', fontsize=10)
ax3.set_ylabel('Average Error Level', fontsize=10)

# ── Draw all lines ────────────────────────────────────────────────────────────
line_groups = {}  # key -> [line_ax1, line_ax2, line_ax3]

for key in keys:
    entry      = grouped[key]
    algorithm  = entry['name']
    comparator = entry['comparator']
    points     = entry['points']

    xs              = sorted(points.keys())
    success_rates   = [points[d]['success_rate'] * 100 for d in xs]
    avg_errors      = [points[d]['avg_error'] for d in xs]
    avg_errors_succ = [points[d]['avg_error_successes'] if points[d]['avg_error_successes'] is not None else float('nan') for d in xs]

    color  = comparator_color_map.get(comparator, 'gray')
    ls     = algorithm_dash_map.get(algorithm, '-')
    marker = algorithm_marker_map.get(algorithm, 'o')

    style = dict(color=color, linestyle=ls, marker=marker, markersize=5, linewidth=1.8)

    l1, = ax1.plot(xs, success_rates,   **style)
    l2, = ax2.plot(xs, avg_errors,      **style)
    l3, = ax3.plot(xs, avg_errors_succ, **style)

    line_groups[key] = [l1, l2, l3]

# ── Checkboxes ────────────────────────────────────────────────────────────────
check_box_ax = plt.axes([0.01, 0.1, 0.14, 0.8])  # [left, bottom, width, height]
check = CheckButtons(
    ax=check_box_ax,
    labels=labels,
    actives=[True] * n_series,
)

# Match checkbox label colors to line colors
for i, (key, label_obj) in enumerate(zip(keys, check.labels)):
    color = comparator_color_map.get(grouped[key]['comparator'], 'gray')
    label_obj.set_color(color)
    label_obj.set_fontsize(8)

def on_toggle(label):
    idx = labels.index(label)
    key = keys[idx]
    for line in line_groups[key]:
        line.set_visible(not line.get_visible())
    plt.draw()

check.on_clicked(on_toggle)

# ── Static legend for line style / marker meaning ────────────────────────────
style_legend = [
    Line2D([0], [0], color='black', linestyle='-',  marker='s', markersize=6, label='dynamic'),
    Line2D([0], [0], color='black', linestyle='-',  marker='o', markersize=6, label='static_foresighted'),
    Line2D([0], [0], color='black', linestyle='--', marker='^', markersize=6, label='static_nearsighted'),
    Line2D([0], [0], color='black', linestyle=':',  marker='D', markersize=6, label='random'),
]
fig.legend(handles=style_legend, title='Algorithm (line/marker)',
           loc='lower center', ncol=4, fontsize=9, bbox_to_anchor=(0.6, -0.02))

plt.tight_layout(rect=[0.16, 0.06, 1.0, 1.0])
plt.show()