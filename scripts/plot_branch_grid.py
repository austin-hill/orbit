#!/usr/bin/env python3
"""Create a 3x3 grid of histograms for producers/consumers in [1,2,4].

Each subplot is a grouped bar chart: x-axis = Branch Prediction strategies,
bars for `copyable=True` and `copyable=False`, y-axis = Time (ms) values (distribution).

Usage:
  python3 scripts/plot_branch_grid.py --data benchmarks/data/optimal_branch_expect.csv --out plots/grid.png --show
"""
from pathlib import Path
import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns


def load_and_clean(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    df = df.rename(columns=lambda s: s.strip())
    df = df.rename(columns={
        "Data Size": "data_size",
        "Copyable": "copyable",
        "Branch Prediction": "branch_pred",
        "Num Producers": "producers",
        "Num Consumers": "consumers",
        "Time (ms)": "time_ms",
    })
    df["producers"] = pd.to_numeric(df["producers"], errors="coerce").fillna(0).astype(int)
    df["consumers"] = pd.to_numeric(df["consumers"], errors="coerce").fillna(0).astype(int)
    df["time_ms"] = pd.to_numeric(df["time_ms"], errors="coerce")
    df["copyable"] = df["copyable"].astype(str).str.lower().map({"true": True, "false": False}).fillna(df["copyable"])
    df["branch_pred"] = df["branch_pred"].astype(str)
    return df


def plot_grid(df: pd.DataFrame, out: Path, show: bool):
    sns.set(style="whitegrid")
    producers = [1, 2, 4]
    consumers = [1, 2, 4]
    fig, axes = plt.subplots(3, 3, figsize=(15, 12), sharey=False)

    for i, p in enumerate(producers):
        for j, c in enumerate(consumers):
            ax = axes[i, j]
            sub = df[(df.producers == p) & (df.consumers == c)]
            if sub.empty:
                ax.set_visible(False)
                continue

            # Use seaborn.barplot to compute mean and show error (std)
            try:
                sns.barplot(data=sub, x="branch_pred", y="time_ms", hue="copyable",
                            estimator=np.mean, ci="sd", dodge=True, capsize=0.1, ax=ax)
            except Exception:
                # fallback: simple groupby mean plot
                grouped = sub.groupby(["branch_pred", "copyable"])['time_ms'].mean().unstack()
                grouped.plot(kind='bar', ax=ax)

            # Set y-limits per subplot based on data range so each axis is scaled appropriately
            values = sub['time_ms'].dropna()
            if not values.empty:
                y_min = float(values.min())
                y_max = float(values.max())
                if y_max == y_min:
                    # single value: add small absolute margin
                    margin = max(1.0, 0.05 * abs(y_max))
                else:
                    margin = 0.08 * (y_max - y_min)
                ax.set_ylim(y_min - margin, y_max + margin)

            ax.set_title(f"P={p}  C={c}")
            ax.set_xlabel("")
            if j == 0:
                ax.set_ylabel("Time (ms)")
            else:
                ax.set_ylabel("")
            ax.set_xticklabels(ax.get_xticklabels(), rotation=45, ha='right')

            # Keep legend only in top-right subplot
            if not (i == 0 and j == 2):
                ax.get_legend().remove()
            else:
                leg = ax.get_legend()
                if leg:
                    leg.set_title("Copyable")

    plt.tight_layout()
    out.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(out)
    if show:
        plt.show()
    plt.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", type=Path, default=Path("benchmarks/data/optimal_branch_expect.csv"))
    ap.add_argument("--out", type=Path, default=Path("plots/branch_grid.png"))
    ap.add_argument("--show", action="store_true")
    args = ap.parse_args()

    df = load_and_clean(args.data)
    plot_grid(df, args.out, args.show)
    print(f"Wrote grid plot to {args.out}")


if __name__ == "__main__":
    main()
