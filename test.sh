#!/usr/bin/env bash
set -euo pipefail

# Fuerza punto decimal (.) para parseo y salida numerica consistente en CSV.
export LC_ALL=C

# =========================================================
# Configuración
# =========================================================
dimensions=(10000 100000 1000000 2500000 4000000 6000000)
nSweeps=15000
iterations=10
thread_counts=(2 4 8 16)
process_counts=(2 4 8 16)
results_dir="results"

mkdir -p "$results_dir"

raw_all="$results_dir/all_runs.csv"
raw_seq="$results_dir/JacobiSec.csv"
raw_thr="$results_dir/JacobiHilos.csv"
raw_proc="$results_dir/JacobiProc.csv"
summary="$results_dir/summary.csv"
speedup="$results_dir/speedup.csv"

# =========================================================
# Compilación
# =========================================================
make clean >/dev/null 2>&1 || true
make >/dev/null

# =========================================================
# Cabeceras CSV
# =========================================================
echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_all"
echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_seq"
echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_thr"
echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_proc"

append_run() {
    local impl="$1"
    local n="$2"
    local sweeps="$3"
    local workers="$4"
    local iter="$5"
    local time_s="$6"
    local target="$7"

    echo "$impl,$n,$sweeps,$workers,$iter,$time_s" >> "$target"
    echo "$impl,$n,$sweeps,$workers,$iter,$time_s" >> "$raw_all"
}

# =========================================================
# Warm-up opcional
# =========================================================
echo "[warmup] Ejecutando calentamiento..."
./JacobiSec 10000 100 >/dev/null 2>&1 || true
./JacobiSecO3 10000 100 >/dev/null 2>&1 || true
./JacobiHilos 10000 100 2 >/dev/null 2>&1 || true
./JacobiProc 10000 100 2 >/dev/null 2>&1 || true

# =========================================================
# Secuencial base (sin optimizacion)
# Orden: iteración -> dimensión
# Así no quedan las 10 repeticiones del mismo n seguidas
# =========================================================
echo "[1/4] Ejecutando secuencial base (O0)..."
for ((iter=1; iter<=iterations; iter++)); do
    echo "  Iteración secuencial $iter/$iterations"
    for dim in "${dimensions[@]}"; do
        t=$(./JacobiSec "$dim" "$nSweeps")
        append_run "seq" "$dim" "$nSweeps" 1 "$iter" "$t" "$raw_seq"
    done
done

# =========================================================
# Secuencial optimizada (-O3)
# Orden: iteración -> dimensión
# =========================================================
echo "[2/4] Ejecutando secuencial optimizada (O3)..."
for ((iter=1; iter<=iterations; iter++)); do
    echo "  Iteración secuencial O3 $iter/$iterations"
    for dim in "${dimensions[@]}"; do
        t=$(./JacobiSecO3 "$dim" "$nSweeps")
        append_run "seq_o3" "$dim" "$nSweeps" 1 "$iter" "$t" "$raw_seq"
    done
done

# =========================================================
# Hilos (sin optimizacion)
# Orden: worker -> iteración -> dimensión
# Se completa una ronda entera de un worker antes de pasar al siguiente
# =========================================================
echo "[3/4] Ejecutando hilos (O0)..."
for workers in "${thread_counts[@]}"; do
    echo "  Worker threads=$workers"
    for ((iter=1; iter<=iterations; iter++)); do
        echo "    Iteración $iter/$iterations"
        for dim in "${dimensions[@]}"; do
            t=$(./JacobiHilos "$dim" "$nSweeps" "$workers")
            append_run "threads" "$dim" "$nSweeps" "$workers" "$iter" "$t" "$raw_thr"
        done
    done
done

# =========================================================
# Procesos (sin optimizacion)
# Orden: worker -> iteración -> dimensión
# =========================================================
echo "[4/4] Ejecutando procesos (O0)..."
for workers in "${process_counts[@]}"; do
    echo "  Worker processes=$workers"
    for ((iter=1; iter<=iterations; iter++)); do
        echo "    Iteración $iter/$iterations"
        for dim in "${dimensions[@]}"; do
            t=$(./JacobiProc "$dim" "$nSweeps" "$workers")
            append_run "processes" "$dim" "$nSweeps" "$workers" "$iter" "$t" "$raw_proc"
        done
    done
done

# =========================================================
# Resumen estadístico
# =========================================================
awk -F, '
BEGIN {
    OFS=","
    print "implementation,n,nsweeps,workers,avg_s,min_s,max_s,stddev_s,runs"
}
NR==1 { next }
{
    key=$1 FS $2 FS $3 FS $4
    count[key]++
    sum[key]+=$6
    sumsq[key]+=($6*$6)
    if (!(key in min) || $6<min[key]) min[key]=$6
    if (!(key in max) || $6>max[key]) max[key]=$6
}
END {
    for (k in count) {
        avg=sum[k]/count[k]
        var=(sumsq[k]/count[k])-(avg*avg)
        if (var<0) var=0
        std=sqrt(var)
        split(k,a,FS)
        printf "%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f,%d\n", a[1],a[2],a[3],a[4],avg,min[k],max[k],std,count[k]
    }
}
' "$raw_all" | sort -t, -k1,1 -k2,2n -k4,4n > "$summary"

# =========================================================
# Speedup y eficiencia
# =========================================================
awk -F, '
BEGIN {
    OFS=","
    print "implementation,n,nsweeps,workers,avg_s,seq_avg_s,speedup,efficiency"
}
NR==1 { next }
$1=="seq" {
    seq[$2","$3]=$5
    next
}
{
    key=$2","$3
    if (key in seq) {
        if ($5+0 > 0 && $4+0 > 0) {
            speedup=seq[key]/$5
            eff=speedup/$4
            printf "%s,%s,%s,%s,%.6f,%.6f,%.6f,%.6f\n", $1,$2,$3,$4,$5,seq[key],speedup,eff
        } else {
            printf "%s,%s,%s,%s,%.6f,%.6f,NA,NA\n", $1,$2,$3,$4,$5,seq[key]
        }
    }
}
' "$summary" | sort -t, -k1,1 -k2,2n -k4,4n > "$speedup"

echo
echo "Listo. Archivos generados en $results_dir/"
echo "  - $raw_all"
echo "  - $raw_seq"
echo "  - $raw_thr"
echo "  - $raw_proc"
echo "  - $summary"
echo "  - $speedup"