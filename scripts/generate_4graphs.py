#!/usr/bin/env python3
"""
Script maestro: Ejecuta las 4 gráficas principales de análisis.
1. Speedup de Threads
2. Speedup de Processes
3. Speedup de Optimizaciones Secuenciales
4. Comparación Global (6 curvas)
"""

import subprocess
import sys
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parent
SCRIPTS = [
    ("plot_speedup_threads.py", "Speedup de Threads"),
    ("plot_speedup_processes.py", "Speedup de Processes"),
    ("plot_sequential_optimizations.py", "Speedup de Optimizaciones Secuenciales"),
    ("plot_6curvas_comparacion_global.py", "Comparación Global (6 curvas)"),
]


def main():
    print("="*80)
    print("Generando 4 gráficas de análisis de resultados Jacobi")
    print("="*80)
    
    failed = []
    successful = []
    
    for i, (script, description) in enumerate(SCRIPTS, start=1):
        script_path = SCRIPTS_DIR / script
        
        print(f"\n[{i}/4] {description}...")
        print("-" * 80)
        
        if not script_path.exists():
            print(f"✗ {script}: archivo no encontrado")
            failed.append((script, description))
            continue
        
        try:
            result = subprocess.run(
                [sys.executable, str(script_path)],
                cwd=SCRIPTS_DIR.parent,
                capture_output=True,
                text=True,
                timeout=60
            )
            
            if result.returncode == 0:
                # Mostrar stdout (tablas y mensajes de éxito)
                if result.stdout:
                    print(result.stdout)
                successful.append((script, description))
            else:
                print(f"✗ {description}: error durante ejecución")
                if result.stderr:
                    print("  Error:", result.stderr[:500])
                failed.append((script, description))
        
        except subprocess.TimeoutExpired:
            print(f"✗ {description}: timeout (>60s)")
            failed.append((script, description))
        except Exception as e:
            print(f"✗ {description}: {e}")
            failed.append((script, description))
    
    # Resumen final
    print("\n" + "="*80)
    print("RESUMEN")
    print("="*80)
    
    if successful:
        print(f"\n✓ Gráficas generadas exitosamente ({len(successful)}/{len(SCRIPTS)}):")
        for script, desc in successful:
            print(f"  • {desc}")
    
    if failed:
        print(f"\n✗ Fallos ({len(failed)}/{len(SCRIPTS)}):")
        for script, desc in failed:
            print(f"  • {desc}")
        return 1
    
    print("\n" + "="*80)
    print("Todas las gráficas están en: results/")
    print("="*80)
    return 0


if __name__ == "__main__":
    sys.exit(main())
