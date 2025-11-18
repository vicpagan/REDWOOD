#!/usr/bin/env python3
import numpy as np
import matplotlib.pyplot as plt
import json
import sys

# Read JSON from stdin or file
if len(sys.argv) > 1:
    with open(sys.argv[1], 'r') as f:
        json_object = json.load(f)
else:
    json_object = json.load(sys.stdin)

data = {}
for key, values in json_object.items():
    data[float(key)] = values
    num_procs = len(values)

    

# Convert to numpy array
d_values = np.array(sorted(data.keys()))
n_values = np.arange(1, num_procs+1)
Z = np.array([data[d] for d in d_values])

# Create meshgrid for contour plot
D, N = np.meshgrid(d_values, n_values)

# Create figure with subplots
fig, (ax1) = plt.subplots(1, 1, figsize=(16, 6))

# Contour plot
contour = ax1.contourf(D, N, Z.T, levels=20, cmap='viridis')
contour_lines = ax1.contour(D, N, Z.T, levels=10, colors='white', 
                             linewidths=0.5, alpha=0.6)
ax1.clabel(contour_lines, inline=True, fontsize=12, fmt='%.2f')
ax1.set_xlabel('deadline (sec)', fontsize=12)
ax1.set_ylabel('number of compute nodes', fontsize=12)
ax1.set_title('Contour Plot', fontsize=14)

plt.tight_layout()
plt.show()
