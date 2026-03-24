#!/usr/bin/env python3
"""
Script maestro: Genera ambas tablas de resumen (promedios y speedup).
"""

import subprocess
import sys
from pathlib import Path


SCRIPTS_DIR = Path(__file__).resolve().parent
SCRIPTS = [
    ("generar_tabla_promedios.py", "Tabla de promedios (wall_s)"),
    ("generar_tabla_speedup.py", "Tabla de speedup"),
]


def main():
    print("="*80)
    print("Generando tablas resumen de resultados")
    print("="*80)
    
    failed = []
    successful = []
    
    for script, description in SCRIPTS:
        script_path = SCRIPTS_DIR / script
        
        print(f"\n[{SCRIPTS.index((script, description)) + 1}/{len(SCRIPTS)}] {description}...")
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
        print(f"\n✓ Tablas generadas exitosamente ({len(successful)}/{len(SCRIPTS)}):")
        for script, desc in successful:
            print(f"  • {desc}")
    
    if failed:
        print(f"\n✗ Fallos ({len(failed)}/{len(SCRIPTS)}):")
        for script, desc in failed:
            print(f"  • {desc}")
        return 1
    
    print("\n" + "="*80)
    print("Tablas guardadas en: results/tablas/")
    print("="*80)
    return 0


if __name__ == "__main__":
    sys.exit(main())
