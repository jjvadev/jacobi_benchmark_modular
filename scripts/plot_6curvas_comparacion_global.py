#!/usr/bin/env python3
"""
Gráfica comparativa: 6 curvas principales
- seq (baseline)
- seq_mem (memoria optimizada)
- seq_o3 (compilador optimizado O3)
- threads 8
- threads 16
- processes 8 y 16

Basado en el patrón de plot_comparacion_6curvas.py del proyecto case1_matrix_mul.
"""

from pathlib import Path
import csv
import numpy as np
import matplotlib.pyplot as plt


BASE_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = BASE_DIR / "results"

SEQ_CSV = RESULTS_DIR / "JacobiSec.csv"
SEQ_MEM_CSV = RESULTS_DIR / "JacobiSecMem.csv"
SEQ_O3_CSV = RESULTS_DIR / "JacobiSecO3.csv"
THREADS_CSV = RESULTS_DIR / "JacobiHilos.csv"
PROC_CSV = RESULTS_DIR / "JacobiProc.csv"

OUTPUT_FIGURE = RESULTS_DIR / "speedup_6curvas_comparacion_global.png"

THREAD_TARGETS = (8, 16)
PROC_TARGETS = (8, 16)


def load_average_sequential(path: Path) -> dict:
    """Cargar tiempos secuenciales promediados por N."""
    grouped = {}
    
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            n = int(row["n"])
            time_s = float(row["time_s"])
            
            if n not in grouped:
                grouped[n] = []
            grouped[n].append(time_s)
    
    return {n: float(np.mean(times)) for n, times in sorted(grouped.items())}


def load_average_by_workers(path: Path, implementation: str, workers_targets: tuple) -> dict:
    """Cargar tiempos promediados por (N, workers)."""
    grouped = {}
    
    with path.open("r", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row["implementation"] != implementation:
                continue
            
            n = int(row["n"])
            workers = int(row["workers"])
            time_s = float(row["time_s"])
            
            if workers not in workers_targets:
                continue
            
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


def speedup_line(baseline: dict, reference_dict: dict, sizes: list, workers: int = None) -> np.ndarray:
    """Calcular línea de speedup."""
    if workers is None:
        return np.array([baseline[n] / reference_dict[n] for n in sizes], dtype=float)
    else:
        return np.array([baseline[n] / reference_dict[n][workers] for n in sizes], dtype=float)


def main():
    # Cargar datos
    seq = load_average_sequential(SEQ_CSV)
    seq_mem = load_average_sequential(SEQ_MEM_CSV)
    seq_o3 = load_average_sequential(SEQ_O3_CSV)
    threads = load_average_by_workers(THREADS_CSV, "threads", THREAD_TARGETS)
    processes = load_average_by_workers(PROC_CSV, "processes", PROC_TARGETS)
    
    # Encontrar tamaños comunes
    common_sizes = sorted(
        set(seq.keys()) & set(seq_mem.keys()) & set(seq_o3.keys()) & 
        set(threads.keys()) & set(processes.keys())
    )
    
    if not common_sizes:
        raise ValueError("No hay tamaños N en común entre todos los datos.")
    
    # Verificar que tenemos datos para workers 8 y 16
    for n in common_sizes:
        for w in THREAD_TARGETS:
            if w not in threads[n]:
                raise ValueError(f"Faltan datos threads={w} para N={n}")
        for w in PROC_TARGETS:
            if w not in processes[n]:
                raise ValueError(f"Faltan datos processes={w} para N={n}")
    
    # Calcular speedups
    x_pos = np.arange(len(common_sizes))
    
    seq_baseline = np.ones(len(common_sizes), dtype=float)
    seq_mem_speedup = speedup_line(seq, seq_mem, common_sizes)
    seq_o3_speedup = speedup_line(seq, seq_o3, common_sizes)
    threads_8_speedup = speedup_line(seq, threads, common_sizes, workers=8)
    threads_16_speedup = speedup_line(seq, threads, common_sizes, workers=16)
    processes_8_speedup = speedup_line(seq, processes, common_sizes, workers=8)
    processes_16_speedup = speedup_line(seq, processes, common_sizes, workers=16)
    
    # Crear gráfica
    plt.style.use("seaborn-v0_8-whitegrid")
    fig, ax = plt.subplots(figsize=(12, 7))
    
    # Plotear las 6 curvas
    ax.plot(x_pos, seq_baseline, marker="o", linewidth=2.0, markersize=7,
            label="seq (baseline O0)", color="#1f77b4")
    ax.plot(x_pos, seq_mem_speedup, marker="s", linewidth=2.0, markersize=7,
            label="seq_mem (O0 memory optimized)", color="#ff7f0e")
    ax.plot(x_pos, seq_o3_speedup, marker="^", linewidth=2.2, markersize=7,
            label="seq_o3 (O3 compiler)", color="#2ca02c")
    ax.plot(x_pos, threads_8_speedup, marker="d", linewidth=2.0, markersize=7,
            label="threads 8", color="#d62728")
    ax.plot(x_pos, threads_16_speedup, marker="v", linewidth=2.0, markersize=7,
            label="threads 16", color="#9467bd")
    ax.plot(x_pos, processes_8_speedup, marker="p", linewidth=2.0, markersize=7,
            label="processes 8", color="#8c564b")
    ax.plot(x_pos, processes_16_speedup, marker="*", linewidth=2.2, markersize=10,
            label="processes 16", color="#e377c2")
    
    ax.set_title("Comparación Global: Speedup de Optimizaciones y Paralelismo", 
                 fontsize=14, fontweight="bold")
    ax.set_xlabel("Tamaño del problema (N)", fontsize=12)
    ax.set_ylabel("Speedup respecto a seq(O0)", fontsize=12)
    ax.set_xticks(x_pos)
    ax.set_xticklabels([str(n) for n in common_sizes])
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best", fontsize=10)
    
    fig.tight_layout()
    fig.savefig(OUTPUT_FIGURE, dpi=300)
    print(f"✓ Gráfica guardada: {OUTPUT_FIGURE}")
    
    # Tabla resumen
    print("\n" + "="*110)
    print("RESUMEN GLOBAL: Speedup de todas las variantes")
    print("="*110)
    print(f"{'N':<10} {'seq':<8} {'seq_mem':<12} {'seq_o3':<12} {'threads_8':<12} {'threads_16':<12} {'proc_8':<12} {'proc_16':<12}")
    print("-"*110)
    
    for i, n in enumerate(common_sizes):
        print(
            f"{n:<10} {seq_baseline[i]:<8.2f}x {seq_mem_speedup[i]:<12.2f}x {seq_o3_speedup[i]:<12.2f}x "
            f"{threads_8_speedup[i]:<12.2f}x {threads_16_speedup[i]:<12.2f}x {processes_8_speedup[i]:<12.2f}x {processes_16_speedup[i]:<12.2f}x"
        )
    
    print("="*110)


if __name__ == "__main__":
    main()
