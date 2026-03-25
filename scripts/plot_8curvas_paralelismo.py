#!/usr/bin/env python3
"""
Grafica comparativa de 8 curvas de paralelismo:
- threads: 2, 4, 8, 16
- processes: 2, 4, 8, 16

Todas las curvas se muestran como speedup respecto a seq (O0).
Eje X principal: N
Referencia secundaria: k, donde N = 2^k + 1
"""

from pathlib import Path
import math
import csv
import numpy as np
import matplotlib.pyplot as plt


BASE_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = BASE_DIR / "results"

SEQ_CSV = RESULTS_DIR / "JacobiSec.csv"
THREADS_CSV = RESULTS_DIR / "JacobiHilos.csv"
PROC_CSV = RESULTS_DIR / "JacobiProc.csv"

OUTPUT_FIGURE = RESULTS_DIR / "speedup_8curvas_paralelismo.png"

THREAD_TARGETS = (2, 4, 8, 16)
PROC_TARGETS = (2, 4, 8, 16)


def valid_n_from_grid(n: int) -> bool:
    """Valida que N cumpla la forma N = 2^k + 1."""
    if n <= 1:
        return False
    k = int(round(math.log2(n - 1)))
    return (1 << k) + 1 == n


def n_to_k(n: int) -> int:
    """Convierte N a k asumiendo N = 2^k + 1."""
    return int(round(math.log2(n - 1)))


def load_average_sequential(path: Path) -> dict:
    """Carga tiempos secuenciales promediados por N."""
    grouped = {}

    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("implementation") != "seq":
                continue

            n = int(row["n"])
            if not valid_n_from_grid(n):
                continue

            time_s = float(row["time_s"])
            if n not in grouped:
                grouped[n] = []
            grouped[n].append(time_s)

    return {n: float(np.mean(times)) for n, times in sorted(grouped.items())}


def load_average_by_workers(path: Path, implementation: str, workers_targets: tuple) -> dict:
    """Carga tiempos promediados por (N, workers) para una implementación dada."""
    grouped = {}

    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row.get("implementation") != implementation:
                continue

            n = int(row["n"])
            if not valid_n_from_grid(n):
                continue

            workers = int(row["workers"])
            if workers not in workers_targets:
                continue

            time_s = float(row["time_s"])

            if n not in grouped:
                grouped[n] = {}
            if workers not in grouped[n]:
                grouped[n][workers] = []
            grouped[n][workers].append(time_s)

    result = {}
    for n, worker_dict in sorted(grouped.items()):
        result[n] = {
            w: float(np.mean(times))
            for w, times in sorted(worker_dict.items())
        }

    return result


def speedup_line(seq_times: dict, parallel_times: dict, sizes: list, workers: int) -> np.ndarray:
    """Calcula speedup para un worker fijo en cada N."""
    return np.array([seq_times[n] / parallel_times[n][workers] for n in sizes], dtype=float)


def ensure_workers_available(data: dict, sizes: list, targets: tuple, label: str) -> None:
    """Verifica que para cada N existan todos los workers objetivo."""
    for n in sizes:
        for w in targets:
            if w not in data[n]:
                raise ValueError(f"Faltan datos {label}={w} para N={n}")


def main():
    seq = load_average_sequential(SEQ_CSV)
    threads = load_average_by_workers(THREADS_CSV, "threads", THREAD_TARGETS)
    processes = load_average_by_workers(PROC_CSV, "processes", PROC_TARGETS)

    common_sizes = sorted(set(seq.keys()) & set(threads.keys()) & set(processes.keys()))
    if not common_sizes:
        raise ValueError("No hay valores N en comun entre seq, threads y processes.")

    ensure_workers_available(threads, common_sizes, THREAD_TARGETS, "threads")
    ensure_workers_available(processes, common_sizes, PROC_TARGETS, "processes")

    x = np.array(common_sizes, dtype=float)

    curves = {
        "threads_2": speedup_line(seq, threads, common_sizes, 2),
        "threads_4": speedup_line(seq, threads, common_sizes, 4),
        "threads_8": speedup_line(seq, threads, common_sizes, 8),
        "threads_16": speedup_line(seq, threads, common_sizes, 16),
        "processes_2": speedup_line(seq, processes, common_sizes, 2),
        "processes_4": speedup_line(seq, processes, common_sizes, 4),
        "processes_8": speedup_line(seq, processes, common_sizes, 8),
        "processes_16": speedup_line(seq, processes, common_sizes, 16),
    }

    plt.style.use("seaborn-v0_8-whitegrid")
    fig, ax = plt.subplots(figsize=(13, 8))

    # 8 curvas (4 threads + 4 processes)
    ax.plot(x, curves["threads_2"], marker="o", linewidth=2.0, markersize=6, label="threads 2", color="#1f77b4")
    ax.plot(x, curves["threads_4"], marker="s", linewidth=2.0, markersize=6, label="threads 4", color="#ff7f0e")
    ax.plot(x, curves["threads_8"], marker="^", linewidth=2.0, markersize=6, label="threads 8", color="#2ca02c")
    ax.plot(x, curves["threads_16"], marker="d", linewidth=2.0, markersize=6, label="threads 16", color="#d62728")

    ax.plot(x, curves["processes_2"], marker="v", linewidth=2.0, markersize=6, label="processes 2", color="#9467bd")
    ax.plot(x, curves["processes_4"], marker="P", linewidth=2.0, markersize=7, label="processes 4", color="#8c564b")
    ax.plot(x, curves["processes_8"], marker="X", linewidth=2.0, markersize=7, label="processes 8", color="#e377c2")
    ax.plot(x, curves["processes_16"], marker="*", linewidth=2.2, markersize=10, label="processes 16", color="#7f7f7f")

    ax.set_title("Comparacion de Paralelismo: 8 curvas (threads/processes)", fontsize=14, fontweight="bold")
    ax.set_xlabel("Tamano del problema N (k como referencia)", fontsize=12)
    ax.set_ylabel("Speedup respecto a seq(O0)", fontsize=12)

    ax.set_xscale("log", base=2)
    ax.set_xticks(common_sizes)
    tick_labels = [f"{n}\n(k={n_to_k(n)})" for n in common_sizes]
    ax.set_xticklabels(tick_labels)

    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=10, ncol=2)

    fig.tight_layout()
    fig.savefig(OUTPUT_FIGURE, dpi=300)
    print(f"✓ Grafica guardada: {OUTPUT_FIGURE}")

    # Resumen por consola
    print("\n" + "=" * 135)
    print("RESUMEN: Speedup de 8 curvas de paralelismo")
    print("=" * 135)
    print(
        f"{'N':<10} {'k':<6} "
        f"{'thr_2':<10} {'thr_4':<10} {'thr_8':<10} {'thr_16':<10} "
        f"{'proc_2':<10} {'proc_4':<10} {'proc_8':<10} {'proc_16':<10}"
    )
    print("-" * 135)

    for i, n in enumerate(common_sizes):
        k = n_to_k(n)
        print(
            f"{n:<10} {k:<6} "
            f"{curves['threads_2'][i]:<10.2f}x {curves['threads_4'][i]:<10.2f}x "
            f"{curves['threads_8'][i]:<10.2f}x {curves['threads_16'][i]:<10.2f}x "
            f"{curves['processes_2'][i]:<10.2f}x {curves['processes_4'][i]:<10.2f}x "
            f"{curves['processes_8'][i]:<10.2f}x {curves['processes_16'][i]:<10.2f}x"
        )

    print("=" * 135)


if __name__ == "__main__":
    main()
