#!/usr/bin/env python3
"""
Grafica speedup de hilos respecto a secuencial base (seq).
Basado en el patrón de plot_speedup_hilos.py del proyecto case1_matrix_mul.
"""

from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np


BASE_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = BASE_DIR / "results"
SEQ_CSV = RESULTS_DIR / "JacobiSec.csv"
THREADS_CSV = RESULTS_DIR / "JacobiHilos.csv"
OUTPUT_FIGURE = RESULTS_DIR / "speedup_threads.png"
THREAD_COUNTS = (2, 4, 8, 16)


def load_sequential_times(path: Path) -> dict:
    """Cargar tiempos secuenciales promediados por tamaño n."""
    df = pd.read_csv(path)
    df = df[df["implementation"] == "seq"]
    grouped = df.groupby("n")["time_s"].mean()
    return grouped.to_dict()


def load_thread_times(path: Path) -> dict:
    """Cargar tiempos de threads promediados por (n, workers)."""
    df = pd.read_csv(path)
    df = df[df["implementation"] == "threads"]
    grouped = df.groupby(["n", "workers"])["time_s"].mean()
    
    result = {}
    for (n, workers), avg_time in grouped.items():
        if n not in result:
            result[n] = {}
        result[n][workers] = avg_time
    
    return result


def main():
    seq_times = load_sequential_times(SEQ_CSV)
    thread_times = load_thread_times(THREADS_CSV)
    
    # Encontrar tamaños comunes
    common_sizes = sorted(set(seq_times.keys()) & set(thread_times.keys()))
    if not common_sizes:
        raise ValueError("No hay tamaños N en común entre secuencial y threads.")
    
    # Preparar gráfica
    plt.style.use("seaborn-v0_8-whitegrid")
    fig, ax = plt.subplots(figsize=(12, 7))
    
    # Colores para cada número de threads
    colors = {2: "#1f77b4", 4: "#ff7f0e", 8: "#2ca02c", 16: "#d62728"}
    
    for thread_count in THREAD_COUNTS:
        speedups = []
        valid_sizes = []
        
        for n in common_sizes:
            if thread_count in thread_times[n]:
                speedup = seq_times[n] / thread_times[n][thread_count]
                speedups.append(speedup)
                valid_sizes.append(n)
        
        if speedups:
            ax.plot(
                valid_sizes,
                speedups,
                marker="o",
                linewidth=2.5,
                markersize=8,
                label=f"{thread_count} hilos",
                color=colors[thread_count]
            )
    
    # Formatting
    ax.set_title("Speedup de Hilos respecto a Secuencial (O0)", fontsize=14, fontweight="bold")
    ax.set_xlabel("Tamaño del problema (N)", fontsize=12)
    ax.set_ylabel("Speedup", fontsize=12)
    ax.set_xscale("log")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=11, loc="best")
    
    fig.tight_layout()
    fig.savefig(OUTPUT_FIGURE, dpi=300)
    print(f"✓ Gráfica guardada: {OUTPUT_FIGURE}")
    
    # Tabla resumen
    print("\n" + "="*70)
    print("RESUMEN: Speedup de Hilos vs Secuencial")
    print("="*70)
    print(f"{'N':<10} {'seq(O0)':<10} {'2 hilos':<12} {'4 hilos':<12} {'8 hilos':<12} {'16 hilos':<12}")
    print("-"*70)
    
    for n in common_sizes:
        seq_t = seq_times[n]
        row = f"{n:<10} {seq_t:<10.6f}"
        
        for tc in THREAD_COUNTS:
            if tc in thread_times[n]:
                speedup = seq_t / thread_times[n][tc]
                row += f" {speedup:<11.2f}x"
            else:
                row += " {'N/A':<11}"
        
        print(row)
    
    print("="*70)


if __name__ == "__main__":
    main()
