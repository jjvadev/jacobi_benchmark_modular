#!/usr/bin/env python3
"""
Genera tabla resumen de speedup para todas las variantes Jacobi (excepto seq baseline).
Lee directamente de los CSVs individuales (JacobiSec.csv, JacobiSecMem.csv, etc.)
"""

from pathlib import Path
import csv
from collections import defaultdict
import numpy as np

import matplotlib.pyplot as plt


BASE_DIR = Path(__file__).resolve().parent.parent
RESULTS_DIR = BASE_DIR / "results"
OUTPUT_DIR = RESULTS_DIR / "tablas"
OUTPUT_CSV = OUTPUT_DIR / "tabla_resumen_speedup.csv"
OUTPUT_PNG = OUTPUT_DIR / "tabla_resumen_speedup.png"

# Archivos individuales por variante
CSV_FILES = {
    "seq": RESULTS_DIR / "JacobiSec.csv",
    "seq_mem": RESULTS_DIR / "JacobiSecMem.csv",
    "seq_o3": RESULTS_DIR / "JacobiSecO3.csv",
    "threads": RESULTS_DIR / "JacobiHilos.csv",
    "processes": RESULTS_DIR / "JacobiProc.csv",
}


def read_csv_data(csv_files: dict) -> dict:
    """
    Lee CSVs individuales y extrae promedios por (implementation, n).
    Para threads y processes, separa por número de workers.
    Retorna dict: {implementation: {n: avg_s}}
    """
    data = defaultdict(dict)
    
    for impl, csv_path in csv_files.items():
        if not csv_path.exists():
            print(f"⚠ Advertencia: No existe {csv_path}, saltando...")
            continue
        
        with csv_path.open("r", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            rows = list(reader)
        
        # Para threads y processes, separar por workers
        if impl in ['threads', 'processes']:
            # Obtener workers únicos
            workers_list = sorted(set(int(row['workers']) for row in rows if 'workers' in row))
            
            for workers in workers_list:
                impl_key = f"{impl}_{workers}"
                times_by_n = defaultdict(list)
                
                for row in rows:
                    if int(row['workers']) == workers:
                        n = int(row["n"])
                        time_s = float(row["time_s"])
                        times_by_n[n].append(time_s)
                
                # Calcular promedio por n
                for n, times in times_by_n.items():
                    data[impl_key][n] = float(np.mean(times))
        else:
            # Para seq, seq_mem, seq_o3: no separar por workers
            times_by_n = defaultdict(list)
            
            for row in rows:
                n = int(row["n"])
                time_s = float(row["time_s"])
                times_by_n[n].append(time_s)
            
            # Calcular promedio por n
            for n, times in times_by_n.items():
                data[impl][n] = float(np.mean(times))
    
    return data


def calculate_speedups(data: dict) -> dict:
    """
    Calcula speedup de cada variante respecto a 'seq' (baseline).
    Retorna dict: {implementation: {n: speedup}}
    """
    speedups = {}
    
    if "seq" not in data:
        raise ValueError("'seq' no encontrado en los datos")
    
    seq_times = data["seq"]
    
    for impl, times in data.items():
        if impl == "seq":
            continue  # No incluir el baseline en el speedup
        
        speedups[impl] = {}
        for n, time_s in times.items():
            if n in seq_times:
                speedups[impl][n] = seq_times[n] / time_s
            else:
                speedups[impl][n] = float('nan')
    
    return speedups


def write_speedup_csv(speedups: dict, output_csv: Path) -> None:
    """Escribe tabla CSV con speedups por variante y tamaño."""
    output_csv.parent.mkdir(parents=True, exist_ok=True)
    
    # Extraer tamaños únicos y ordenar
    all_sizes = set()
    for impl_data in speedups.values():
        all_sizes.update(impl_data.keys())
    sizes = sorted(all_sizes)
    
    # Ordenar implementaciones: seq_mem, seq_o3, threads_2/4/8/16, processes_2/4/8/16
    def sort_key(impl):
        order = ["seq_mem", "seq_o3"]
        for idx, prefix in enumerate(order):
            if impl == prefix:
                return (idx, 0, 0)
        
        if impl.startswith("threads_"):
            workers = int(impl.split("_")[1])
            return (2, 0, workers)
        elif impl.startswith("processes_"):
            workers = int(impl.split("_")[1])
            return (2, 1, workers)
        
        return (len(order), 0, 0)
    
    implementations = sorted(speedups.keys(), key=sort_key)
    
    with output_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.writer(f)
        
        # Encabezado
        writer.writerow(["Variante"] + [str(n) for n in sizes])
        
        # Datos
        for impl in implementations:
            row = [impl]
            for n in sizes:
                if n in speedups[impl]:
                    speedup_val = speedups[impl][n]
                    # Formatear como speedup (ej: "3.45x")
                    if isinstance(speedup_val, float) and np.isnan(speedup_val):
                        row.append("N/A")
                    else:
                        row.append(f"{float(speedup_val):.4f}x")
                else:
                    row.append("N/A")
            writer.writerow(row)


def write_speedup_png(csv_path: Path, output_png: Path) -> None:
    """Genera tabla PNG desde CSV."""
    output_png.parent.mkdir(parents=True, exist_ok=True)
    
    with csv_path.open("r", encoding="utf-8") as f:
        rows = list(csv.reader(f))
    
    headers = rows[0]
    data_rows = rows[1:]
    
    n_rows = len(data_rows) + 1
    n_cols = len(headers)
    fig_width = max(12, n_cols * 1.35)
    fig_height = max(6, n_rows * 0.6)
    
    fig, ax = plt.subplots(figsize=(fig_width, fig_height))
    ax.axis("off")
    
    # Crear tabla
    table = ax.table(
        cellText=data_rows,
        colLabels=headers,
        loc="center",
        cellLoc="center",
    )
    
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1.0, 1.8)
    
    # Formato: encabezado
    for col_idx in range(n_cols):
        cell = table[(0, col_idx)]
        cell.set_facecolor("#70AD47")
        cell.set_text_props(weight="bold", color="white")
    
    # Formato: filas alternadas
    for row_idx in range(1, n_rows):
        label_cell = table[(row_idx, 0)]
        label_cell.set_text_props(weight="bold")
        label_cell.set_facecolor("#E2F0D9")
        
        if row_idx % 2 == 0:
            for col_idx in range(n_cols):
                table[(row_idx, col_idx)].set_facecolor("#F2F2F2")
    
    plt.title("Tabla resumen: Speedup respecto a seq(O0) por variante y tamaño", 
              fontsize=13, fontweight="bold", pad=20)
    fig.tight_layout()
    fig.savefig(output_png, dpi=300, bbox_inches="tight")
    plt.close(fig)


def main():
    print("Leyendo datos de CSVs individuales...")
    data = read_csv_data(CSV_FILES)
    
    if not data:
        raise ValueError("No se pudieron leer datos de los CSVs")
    
    print(f"Variantes encontradas: {list(data.keys())}")
    print(f"Tamaños encontrados: {sorted(set(n for d in data.values() for n in d.keys()))}")
    
    print("\nCalculando speedups (respecto a seq baseline)...")
    speedups = calculate_speedups(data)
    
    print(f"\nGenerando CSV: {OUTPUT_CSV}")
    write_speedup_csv(speedups, OUTPUT_CSV)
    
    print(f"Generando PNG: {OUTPUT_PNG}")
    write_speedup_png(OUTPUT_CSV, OUTPUT_PNG)
    
    print("\n✓ Tablas de speedup generadas exitosamente")


if __name__ == "__main__":
    main()
