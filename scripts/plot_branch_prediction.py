#!/usr/bin/env python3
"""Plot branch-prediction benchmark results to help choose the best strategy.

Usage examples:
  python3 scripts/plot_branch_prediction.py --data-dir benchmarks/data --out plots --show

This script reads all CSVs in the data directory, aggregates mean times,
produces line plots (Time vs Data Size per strategy) and heatmaps showing
the best branch prediction strategy for each producers/consumers grid.
"""
import argparse
from pathlib import Path
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns


def load_data(data_dir: Path):
    files = sorted(data_dir.glob("*.csv"))
    if not files:
        raise SystemExit(f"No CSV files found in {data_dir}")
    dfs = []
    for f in files:
        try:
            df = pd.read_csv(f)
            df["_source_file"] = f.name
            dfs.append(df)
        except Exception as e:
            print(f"Warning: failed to read {f}: {e}")
    return pd.concat(dfs, ignore_index=True)


def sanitize(df: pd.DataFrame) -> pd.DataFrame:
    df = df.rename(columns=lambda s: s.strip())
    # normalize columns we expect
    df = df.rename(columns={
        "Data Size": "data_size",
        "Copyable": "copyable",
        "Branch Prediction": "branch_pred",
        "Num Producers": "producers",
        "Num Consumers": "consumers",
        "Time (ms)": "time_ms",
    })
    # types
    df["data_size"] = pd.to_numeric(df["data_size"], errors="coerce").astype(int)
    if "copyable" in df.columns:
        df["copyable"] = df["copyable"].astype(str).str.lower().map({"true": True, "false": False}).fillna(df["copyable"])
    df["branch_pred"] = df["branch_pred"].astype(str)
    df["producers"] = pd.to_numeric(df["producers"], errors="coerce").fillna(0).astype(int)
    df["consumers"] = pd.to_numeric(df["consumers"], errors="coerce").fillna(0).astype(int)
    df["time_ms"] = pd.to_numeric(df["time_ms"], errors="coerce")
    return df


def aggregate(df: pd.DataFrame):
    group_cols = ["data_size", "copyable", "branch_pred", "producers", "consumers"]
    agg = df.groupby(group_cols, dropna=False)["time_ms"].agg(["mean", "std", "count"]).reset_index()
    agg = agg.rename(columns={"mean": "time_mean", "std": "time_std", "count": "n"})
    return agg


def best_by_params(agg: pd.DataFrame) -> pd.DataFrame:
    # for each parameter combination except branch_pred, pick the branch_pred with lowest mean time
    by = ["data_size", "copyable", "producers", "consumers"]
    idx = agg.groupby(by)["time_mean"].idxmin()
    best = agg.loc[idx].reset_index(drop=True)
    best = best.rename(columns={"branch_pred": "best_branch_pred", "time_mean": "best_time_mean"})
    return best


def plot_lines(agg: pd.DataFrame, out_dir: Path, show: bool):
    sns.set(style="whitegrid")
    # For each (producers, consumers, copyable) create a line plot
    combos = agg[["producers", "consumers", "copyable"]].drop_duplicates()
    for _, row in combos.iterrows():
        p, c, copyable = int(row.producers), int(row.consumers), row.copyable
        sub = agg[(agg.producers == p) & (agg.consumers == c) & (agg.copyable == copyable)]
        if sub.empty:
            continue
        plt.figure(figsize=(8, 5))
        ax = sns.lineplot(data=sub, x="data_size", y="time_mean", hue="branch_pred", marker="o")
        ax.set_title(f"Time vs Data Size — prod={p} cons={c} copyable={copyable}")
        ax.set_xlabel("Data Size")
        ax.set_ylabel("Time (ms)")
        plt.legend(title="Branch Prediction", bbox_to_anchor=(1.05, 1), loc="upper left")
        plt.tight_layout()
        out = out_dir / f"time_lines_p{p}_c{c}_copy{copyable}.png"
        plt.savefig(out)
        if show:
            plt.show()
        plt.close()


def plot_heatmaps(best: pd.DataFrame, out_dir: Path, show: bool):
    # For each data_size and copyable, create a producers x consumers grid colored by best strategy
    strategies = sorted(best.best_branch_pred.unique())
    strategy_to_int = {s: i for i, s in enumerate(strategies)}
    cmap = plt.get_cmap("tab10")
    for data_size in sorted(best.data_size.unique()):
        for copyable in sorted(best.copyable.unique()):
            sub = best[(best.data_size == data_size) & (best.copyable == copyable)]
            if sub.empty:
                continue
            p_vals = sorted(sub.producers.unique())
            c_vals = sorted(sub.consumers.unique())
            grid = np.full((len(c_vals), len(p_vals)), np.nan)
            for i, cv in enumerate(c_vals):
                for j, pv in enumerate(p_vals):
                    row = sub[(sub.producers == pv) & (sub.consumers == cv)]
                    if not row.empty:
                        grid[i, j] = strategy_to_int[row.iloc[0].best_branch_pred]
            plt.figure(figsize=(6, 4))
            sns.heatmap(grid, annot=True, fmt=".0f", cmap=cmap, cbar=False,
                        xticklabels=p_vals, yticklabels=c_vals)
            plt.xlabel("Producers")
            plt.ylabel("Consumers")
            plt.title(f"Best Branch Strategy — data_size={data_size} copyable={copyable}")
            # build legend mapping
            handles = [plt.Line2D([0], [0], marker='s', color='w', label=s,
                                  markerfacecolor=cmap(i % 10), markersize=10) for s, i in strategy_to_int.items()]
            plt.legend(handles=handles, title="Strategy", bbox_to_anchor=(1.05, 1), loc="upper left")
            plt.tight_layout()
            out = out_dir / f"best_strategy_heatmap_ds{data_size}_copy{copyable}.png"
            plt.savefig(out)
            if show:
                plt.show()
            plt.close()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--data-dir", type=Path, default=Path("benchmarks/data"))
    p.add_argument("--out", type=Path, default=Path("plots"), help="output directory for plots and summary")
    p.add_argument("--show", action="store_true", help="show plots interactively")
    args = p.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    df = load_data(args.data_dir)
    df = sanitize(df)
    agg = aggregate(df)
    best = best_by_params(agg)
    # write summary
    summary_csv = args.out / "best_branch_by_params.csv"
    best.to_csv(summary_csv, index=False)
    print(f"Wrote summary to {summary_csv}")

    plot_lines(agg, args.out, args.show)
    plot_heatmaps(best, args.out, args.show)
    print(f"Saved plots to {args.out}")


if __name__ == "__main__":
    main()
