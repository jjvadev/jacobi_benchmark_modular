#!/usr/bin/env bash
set -euo pipefail

# Fuerza punto decimal (.) para parseo y salida numerica consistente en CSV.
export LC_ALL=C

# =========================================================
# Configuracion
# =========================================================
K_VALUES_STR="${K_VALUES:- 5 10 12 14}"
NSWEEPS="${NSWEEPS:-5000000}"
ITERATIONS="${ITERATIONS:-4}"
PROCESS_COUNTS_STR="${PROCESS_COUNTS:-2 4 8 16}"
TOLERANCE="${TOLERANCE:-1e-30}"

read -r -a k_values <<< "$K_VALUES_STR"
read -r -a process_counts <<< "$PROCESS_COUNTS_STR"

nSweeps="$NSWEEPS"
iterations="$ITERATIONS"
results_dir="results"

mkdir -p "$results_dir"

raw_all="$results_dir/all_runs.csv"
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
echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_proc"
if [ ! -f "$raw_all" ]; then
    echo "implementation,n,nsweeps,workers,iteration,time_s" > "$raw_all"
fi

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

nodes_from_k() {
    local k="$1"
    if (( k < 1 || k > 29 )); then
        return 1
    fi
    echo $(( (1 << k) + 1 ))
}

# =========================================================
# Warm-up opcional
# =========================================================
echo "[warmup] Ejecutando calentamiento..."
./JacobiProc 14 100 2 >/dev/null 2>&1 || true

# =========================================================
# Procesos (sin optimizacion)
# Orden: worker -> iteración -> dimensión
# =========================================================
echo "[1/1] Ejecutando procesos (O0)..."
for workers in "${process_counts[@]}"; do
    echo "  Worker processes=$workers"
    for ((iter=1; iter<=iterations; iter++)); do
        echo "    Iteración $iter/$iterations"
        for k in "${k_values[@]}"; do
            n=$(nodes_from_k "$k")
            t=$(./JacobiProc "$k" "$nSweeps" "$workers" "$TOLERANCE")
            append_run "processes" "$n" "$nSweeps" "$workers" "$iter" "$t" "$raw_proc"
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
NR>1 && ($1=="threads" || $1=="processes") {
    key=$2","$3
    if (key in seq) {
        speedup=$5/seq[key]
        efficiency=speedup/$4
        printf "%s,%s,%s,%s,%.6f,%.6f,%.4f,%.4f\n", $1,$2,$3,$4,$5,seq[key],speedup,efficiency
    }
}
' "$raw_all" > "$speedup"

echo ""
echo "=========================================="
echo "✓ Pruebas de procesos completadas"
echo "=========================================="
echo "Resultados guardados en: $results_dir/"
