#!/usr/bin/env python3
import csv
import math
import os
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


def read_summary(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            row["n"] = int(row["n"])
            row["workers"] = int(row["workers"])
            row["avg_s"] = float(row["avg_s"])
            row["stddev_s"] = float(row["stddev_s"])
            rows.append(row)
    return rows


def read_speedup(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            row["n"] = int(row["n"])
            row["workers"] = int(row["workers"])
            row["speedup"] = float(row["speedup"])
            row["efficiency"] = float(row["efficiency"])
            rows.append(row)
    return rows


def plot_time_vs_n(summary_rows, out_dir):
    grouped = defaultdict(list)
    for row in summary_rows:
        label = row["implementation"] if row["implementation"] == "seq" else f"{row['implementation']}-{row['workers']}"
        grouped[label].append((row["n"], row["avg_s"]))

    plt.figure(figsize=(10, 6))
    for label, values in sorted(grouped.items()):
        values.sort()
        xs = [v[0] for v in values]
        ys = [v[1] for v in values]
        plt.plot(xs, ys, marker="o", label=label)

    plt.xscale("log")
    plt.yscale("log")
    plt.xlabel("n")
    plt.ylabel("Tiempo promedio (s)")
    plt.title("Jacobi 1D: tiempo promedio vs tamaño de malla")
    plt.grid(True, which="both", linestyle="--", alpha=0.5)
    plt.legend()
    plt.tight_layout()
    plt.savefig(os.path.join(out_dir, "time_vs_n.png"), dpi=200)
    plt.close()


def plot_speedup(speedup_rows, out_dir):
    by_n = defaultdict(list)
    for row in speedup_rows:
        by_n[row["n"]].append((row["implementation"], row["workers"], row["speedup"]))

    for n, values in sorted(by_n.items()):
        plt.figure(figsize=(10, 6))
        thr = sorted([(w, s) for impl, w, s in values if impl == "threads"])
        pro = sorted([(w, s) for impl, w, s in values if impl == "processes"])

        if thr:
            plt.plot([x for x, _ in thr], [y for _, y in thr], marker="o", label=f"threads n={n}")
        if pro:
            plt.plot([x for x, _ in pro], [y for _, y in pro], marker="s", label=f"processes n={n}")

        if thr or pro:
            max_workers = max([x for x, _ in thr + pro])
            plt.plot([1, max_workers], [1, max_workers], linestyle="--", label="ideal")

        plt.xlabel("Workers")
        plt.ylabel("Speedup")
        plt.title(f"Speedup para n={n}")
        plt.grid(True, linestyle="--", alpha=0.5)
        plt.legend()
        plt.tight_layout()
        plt.savefig(os.path.join(out_dir, f"speedup_n_{n}.png"), dpi=200)
        plt.close()


def main():
    results_dir = sys.argv[1] if len(sys.argv) > 1 else "results"
    summary_csv = os.path.join(results_dir, "summary.csv")
    speedup_csv = os.path.join(results_dir, "speedup.csv")

    if not os.path.exists(summary_csv):
        print(f"No existe {summary_csv}")
        sys.exit(1)
    if not os.path.exists(speedup_csv):
        print(f"No existe {speedup_csv}")
        sys.exit(1)

    summary_rows = read_summary(summary_csv)
    speedup_rows = read_speedup(speedup_csv)
    plot_time_vs_n(summary_rows, results_dir)
    plot_speedup(speedup_rows, results_dir)
    print(f"Graficos listos en {results_dir}/")


if __name__ == "__main__":
    main()
