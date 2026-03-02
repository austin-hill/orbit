#!/usr/bin/env python3

######################################################################################
# NOTE to save time this script was generated with AI. This is not good code
######################################################################################

"""
Plot throughput histograms for different queue types with error bars.
Throughput is calculated as: Num Values / (Time in seconds)
"""

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path
import math

import re


def _name_sort_key(name):
    """Natural sort key: split string into non-digit and digit parts.

    Each part becomes a tuple (is_text_flag, value) where numbers are (0, int)
    and text is (1, lowercased string). This makes comparisons numeric-aware
    even for embedded numbers (e.g. abc20def100 > abc20def80 handled correctly).
    """
    s = str(name).strip()
    parts = re.split(r'(\d+)', s)
    key = []
    for p in parts:
        if p == '':
            continue
        if p.isdigit():
            key.append((0, int(p)))
        else:
            key.append((1, p.lower()))
    return tuple(key)

# Read the CSV file
csv_path = "benchmarks/data/pause_lengths/throughput_data.csv"
df = pd.read_csv(csv_path)

# Clean column names (remove leading/trailing spaces)
df.columns = df.columns.str.strip()

# Calculate throughput: (num_values_per_producer * num_producers) / time_in_seconds
df['Throughput'] = df['Num Values'] / (df['Time (ms)'] / 1000)

# Get unique queue types, producers, and consumers
queue_types = df['Name'].unique()
producer_counts = sorted(df['Num Producers'].unique())
consumer_counts = sorted(df['Num Consumers'].unique())

# Build list of non-empty producer/consumer combinations
combos = []
for num_producers in producer_counts:
    for num_consumers in consumer_counts:
        subset = df[(df['Num Producers'] == num_producers) & (df['Num Consumers'] == num_consumers)]
        if not subset.empty:
            combos.append((num_producers, num_consumers, subset))

if not combos:
    print("No data combinations found to plot.")
    raise SystemExit(0)

# Determine grid size
nplots = len(combos)
ncols = min(4, nplots)
nrows = (nplots + ncols - 1) // ncols

# Create a single large figure with a grid of subplots
fig, axes = plt.subplots(nrows=nrows, ncols=ncols, figsize=(15 * ncols, 12 * nrows), squeeze=False)
axes_flat = axes.flatten()

for idx, (num_producers, num_consumers, subset) in enumerate(combos):
    ax = axes_flat[idx]
    stats = subset.groupby('Name')['Throughput'].agg(['mean', 'std', 'count'])
    stats['stderr'] = stats['std'] / np.sqrt(stats['count'])

    # Reorder stats index so numeric-like names sort numerically (e.g. '20' before '100')
    try:
        new_order = sorted(list(stats.index), key=_name_sort_key)
        stats = stats.loc[new_order]
    except Exception:
        # If reordering fails for any reason, keep original order
        pass

    x_pos = np.arange(len(stats))
    means = stats['mean'].values
    stderrs = stats['stderr'].values

    bars = ax.bar(x_pos, means, yerr=stderrs, capsize=4, alpha=0.8, error_kw={'linewidth': 1.5})
    colors = plt.cm.Set3(np.linspace(0, 1, len(stats)))
    for bar, color in zip(bars, colors):
        bar.set_color(color)

    ax.set_xticks(x_pos)
    ax.set_xticklabels([name for name in stats.index], rotation=45, ha='right')
    ax.set_xlabel('Queue Type')
    ax.set_ylabel('Throughput (values/sec)')
    ax.set_title(f'{num_producers}P × {num_consumers}C')
    ax.grid(axis='y', alpha=0.25, linestyle='--')
    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda x, p: f'{x/1e6:.1f}M'))

# Hide any unused subplots
for j in range(nplots, len(axes_flat)):
    axes_flat[j].axis('off')

plt.tight_layout()

# Save combined figure
outpath = f"benchmarks/plots/pause_lengths/throughput.png"
fig.savefig(outpath, dpi=150, bbox_inches='tight')
plt.close(fig)

print(f"Saved combined grid: {outpath}")
