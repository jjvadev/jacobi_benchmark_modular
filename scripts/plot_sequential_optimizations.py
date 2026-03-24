#!/usr/bin/env python3
"""
Grafica comparación de optimizaciones secuenciales:
- seq (baseline O0)
- seq_mem (memoria optimizada O0)
- seq_o3 (compiler optimizado O3)
"""

from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt


BASE_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = BASE_DIR / "results"
SEQ_CSV = RESULTS_DIR / "JacobiSec.csv"
SEQ_MEM_CSV = RESULTS_DIR / "JacobiSecMem.csv"
SEQ_O3_CSV = RESULTS_DIR / "JacobiSecO3.csv"
OUTPUT_FIGURE = RESULTS_DIR / "speedup_sequential_optimizations.png"


def load_times_by_variant(path: Path, impl_name: str) -> dict:
    """Cargar tiempos promediados por tamaño n."""
    df = pd.read_csv(path)
    df = df[df["implementation"] == impl_name]
    grouped = df.groupby("n")["time_s"].mean()
    return grouped.to_dict()


def main():
    seq_times = load_times_by_variant(SEQ_CSV, "seq")
    seq_mem_times = load_times_by_variant(SEQ_MEM_CSV, "seq_mem")
    seq_o3_times = load_times_by_variant(SEQ_O3_CSV, "seq_o3")
    
    # Encontrar tamaños comunes
    common_sizes = sorted(
        set(seq_times.keys()) & set(seq_mem_times.keys()) & set(seq_o3_times.keys())
    )
    if not common_sizes:
        raise ValueError("No hay tamaños N en común entre todas las variantes.")
    
    # Calcular speedups relativos a seq baseline
    mem_speedup = [seq_times[n] / seq_mem_times[n] for n in common_sizes]
    o3_speedup = [seq_times[n] / seq_o3_times[n] for n in common_sizes]
    
    # Preparar gráfica de speedup con baseline en y=1
    fig, ax = plt.subplots(figsize=(12, 7))
    
    # Línea baseline seq en y=1
    ax.axhline(y=1, color="#1f77b4", linewidth=2.5, marker="o", markersize=8,
               label="seq (O0 baseline)", linestyle="-", alpha=0.9)
    
    # Línea seq_mem
    ax.plot(
        common_sizes,
        mem_speedup,
        marker="s",
        linewidth=2.5,
        markersize=8,
        label="seq_mem (O0 memory optimized)",
        color="#ff7f0e"
    )
    
    # Línea seq_o3
    ax.plot(
        common_sizes,
        o3_speedup,
        marker="^",
        linewidth=2.5,
        markersize=8,
        label="seq_o3 (O3 compiler)",
        color="#2ca02c"
    )
    
    ax.set_title("Speedup: Optimizaciones Secuenciales vs seq(O0)", fontsize=14, fontweight="bold")
    ax.set_xlabel("Tamaño del problema (N)", fontsize=12)
    ax.set_ylabel("Speedup", fontsize=12)
    ax.set_xscale("log")
    ax.grid(True, alpha=0.3)
    ax.legend(fontsize=11, loc="best")
    
    fig.tight_layout()
    fig.savefig(OUTPUT_FIGURE, dpi=300)
    print(f"✓ Gráfica guardada: {OUTPUT_FIGURE}")
    
    # Tabla resumen
    print("\n" + "="*85)
    print("RESUMEN: Optimizaciones Secuenciales")
    print("="*85)
    print(f"{'N':<10} {'seq(O0)':<12} {'seq_mem(O0)':<14} {'seq_o3(O3)':<12} {'Mem Speedup':<14} {'O3 Speedup':<12}")
    print("-"*85)
    
    for i, n in enumerate(common_sizes):
        seq_t = seq_times[n]
        mem_t = seq_mem_times[n]
        o3_t = seq_o3_times[n]
        mem_sp = mem_speedup[i]
        o3_sp = o3_speedup[i]
        
        print(
            f"{n:<10} {seq_t:<12.6f} {mem_t:<14.6f} {o3_t:<12.6f} "
            f"{mem_sp:<14.2f}x {o3_sp:<12.2f}x"
        )
    
    print("="*85)


if __name__ == "__main__":
    main()
