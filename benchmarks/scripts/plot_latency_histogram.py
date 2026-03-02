#!/usr/bin/env python3

######################################################################################
# NOTE to save time this script was generated with AI. This is not good code
######################################################################################

"""Plot normalized latency per queue type with error bars.

Reads a CSV with columns: Name, Queue Size, Data Type, Num Bounces, Time (ms)
and groups by a parsed queue type extracted from the `Name` column.
"""
import argparse
import os
import re
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

def main():
    parser = argparse.ArgumentParser(description="Plot normalized latency per queue type.")
    parser.add_argument("--input", "-i", default="benchmarks/data/pause_lengths/latency_data.csv")
    parser.add_argument("--output", "-o", default="benchmarks/plots/pause_lengths/latency.png")
    parser.add_argument("--show", action="store_true", help="Show the plot interactively")
    args = parser.parse_args()

    df = pd.read_csv(args.input)

    # Normalize time by Num Bounces
    # Strip whitespace from column names to handle CSVs with spaces after commas
    df.columns = df.columns.str.strip()
    if "Time (ms)" not in df.columns or "Num Bounces" not in df.columns:
        raise RuntimeError("Expected columns 'Time (ms)' and 'Num Bounces' in CSV (after stripping header whitespace)")

    df = df.dropna(subset=["Time (ms)", "Num Bounces"])
    # Compute normalized time in nanoseconds per bounce
    df["normalized_ns"] = df["Time (ms)"] * 1e6 / df["Num Bounces"]

    # Parse queue type
    df["queue_type"] = df["Name"].astype(str)

    # Group and compute mean and SEM (units: ns per bounce)
    grouped = df.groupby("queue_type")["normalized_ns"].agg(["mean", "std", "count"]).reset_index()
    grouped["sem"] = grouped["std"] / np.sqrt(grouped["count"])

    # Sort by mean for nicer plotting
    grouped = grouped.sort_values("mean")

    labels = grouped["queue_type"].tolist()
    means = grouped["mean"].to_numpy()
    sems = grouped["sem"].to_numpy()

    os.makedirs(os.path.dirname(args.output), exist_ok=True)

    fig, ax = plt.subplots(figsize=(10, 6))
    x = np.arange(len(labels))
    ax.bar(x, means, yerr=sems, capsize=6, color="C0")
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=45, ha="right")
    ax.set_ylabel("Time per bounce (ns)")
    ax.set_title("Normalized Latency by Queue Type (ns, mean ± SEM)")
    plt.tight_layout()
    fig.savefig(args.output, dpi=200)

    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
