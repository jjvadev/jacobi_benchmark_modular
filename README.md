# Jacobi 1D modularizado en C

Proyecto modular para comparar tres versiones del algoritmo de Jacobi 1D:

- `JacobiSec`: secuencial
- `JacobiHilos`: paralela con `pthread`
- `JacobiProc`: paralela con `fork` + memoria compartida `mmap` + `pipe`

## Estructura

- `include/jacobi.h`: definiciones comunes
- `src/common.c`: temporizador, reserva e inicializacion
- `src/jacobi_seq.c`: kernel secuencial
- `src/jacobi_threads.c`: kernel con hilos
- `src/jacobi_processes.c`: kernel con procesos persistentes
- `src/main_*.c`: ejecutables separados
- `test.sh`: corrida completa de benchmarks y generacion de CSV
- `test_quick.sh`: prueba corta para validar que todo compile y corra
- `scripts/make_summary.awk`: resume promedio, min, max y desviacion estandar
- `scripts/plot_results.py`: genera graficos PNG a partir de los CSV

## Compilar

```bash
make
```

En Fedora:

```bash
sudo dnf install gcc make glibc-devel python3 python3-matplotlib
```

## Ejecutar manualmente

```bash
./JacobiSec 100000 15000
./JacobiHilos 100000 15000 8
./JacobiProc 100000 15000 8
```

## Ejecutar todas las pruebas

Configuracion por defecto de `test.sh`:

- dimensiones: `10000 100000 1000000 2500000 4000000 6000000`
- barridos: `15000`
- repeticiones: `10`
- hilos: `2 4 8 16 32`
- procesos: `2 4 8 16 32`

```bash
chmod +x test.sh
./test.sh
```

Prueba rapida:

```bash
chmod +x test_quick.sh
./test_quick.sh
```

Tambien puedes sobreescribir parametros sin editar el script:

```bash
DIMS="10000 100000" NSWEEPS=5000 ITERATIONS=5 THREAD_COUNTS="2 4 8" PROCESS_COUNTS="2 4 8" ./test.sh
```

## CSV generados

Dentro de `results/`:

- `all_runs.csv`: todas las ejecuciones crudas
- `JacobiSec.csv`: solo secuencial
- `JacobiHilos.csv`: solo hilos
- `JacobiProc.csv`: solo procesos
- `summary.csv`: promedio, min, max, desviacion estandar por configuracion
- `speedup.csv`: speedup y eficiencia contra la version secuencial

### Formato de `all_runs.csv`

```csv
implementation,n,nsweeps,workers,iteration,time_s
seq,10000,15000,1,1,0.123456
threads,10000,15000,4,1,0.045612
processes,10000,15000,4,1,0.081331
```

### Formato de `summary.csv`

```csv
implementation,n,nsweeps,workers,runs,avg_s,min_s,max_s,stddev_s
threads,100000,15000,8,10,0.532100,0.529001,0.540022,0.003178
```

### Formato de `speedup.csv`

```csv
implementation,n,nsweeps,workers,avg_s,seq_avg_s,speedup,efficiency
threads,100000,15000,8,0.532100,3.941000,7.406502,0.925813
```

## Graficar con Python

```bash
python3 scripts/plot_results.py results
```

Esto genera:

- `results/time_vs_n.png`
- `results/speedup_n_*.png`

## Notas para el informe

1. **Secuencial**: sirve como linea base.
2. **Hilos**: suele ser la mejor opcion en memoria compartida por menor costo de comunicacion.
3. **Procesos**: aqui se usa `fork` de bajo nivel, pero se evita el error comun de crear procesos en cada barrido. Los hijos se crean una sola vez y se sincronizan por `pipe`.
4. **CSV listos para informe**: puedes usar `summary.csv` para tablas y `speedup.csv` para discutir escalabilidad.
