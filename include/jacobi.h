/*
 * ============================================================
 *  JACOBI_H
 * ============================================================
 *
 *  Este archivo define las estructuras de datos y las
 *  interfases (prototipos de funciones) comunes a TODAS
 *  las implementaciones del metodo iterativo de Jacobi
 *  para resolver la ecuacion de Poisson 1D.
 *
 *  PROPOSITO:
 *  - Definir ctx (JacobiContext): el contenedor de datos
 *    que fluye a traves de todas las funciones.
 *  - Declarar las funciones de inicializacion, limpieza
 *    y las 4 variantes del algoritmo (serial O0, serial
 *    optimizado memoria, hilos, procesos).
 *
 *  FILOSOFIA:
 *  Cada implementacion comparte la MISMA estructura de datos
 *  y la MISMA interfaz publica. Las diferencias internas
 *  (serial vs paralelo, O0 vs O3) son detalles de como se
 *  organizan los calculos, pero el problema y sus parametros
 *  son identicos en todas partes. Esto garantiza que el
 *  speedup mida UNICAMENTE el efecto del paralelismo,
 *  no cambios de algoritmo.
 * ============================================================ */

#ifndef JACOBI_H
#define JACOBI_H

#include <stddef.h>

#define MAX_WORKERS 32  /* limite maximo de hilos/procesos */

/* ============================================================
 *  LIMITES DEL SISTEMA
 * ============================================================
 *  Contiene todos los parametros y arreglos necesarios para
 *  reproducir el problema de Poisson y guardas los resultados.
 */
typedef struct {
    /* PARAMETROS DE ENTRADA */
    int n;              /* numero de nodos de la malla (incluye fronteras) */
    int nsweeps;        /* iteraciones maximas permitidas */
    int workers;        /* numero de hilos o procesos */

    /* RESULTADOS */
    int sweeps_done;    /* iteraciones realizadas */

    /* PARAMETROS GEOMETRICOS (precalculados) */
    double h;           /* espaciamiento de la malla: h = 1/(n-1) */
    double h2;          /* h al cuadrado, aparece en la formula Jacobi */

    /* CONVERGENCIA */
    double tolerance;   /* criterio de parada: umbral RMS residual */
    double last_error;  /* residual RMS de la ultima iteracion */

    /* ARREGLOS DINAMICOS */
    double *u;          /* solucion actual u_i ≈ u(x_i) */
    double *utmp;       /* solucion nueva durante calculo (u_new) */
    double *f;          /* termino forzante en cada nodo */
} JacobiContext;

/* ============================================================
 *  FUNCIONES PUBLICAS
 * ============================================================ */

/* Retorna tiempo actual en segundos (CLOCK_MONOTONIC) */
double wall_time_seconds(void);

/* Reserva memoria y inicializa contexto */
int init_context_heap(JacobiContext *ctx, int n, int nsweeps, int workers);

/* Libera memoria dinamica do contexto */
void free_context_heap(JacobiContext *ctx);

/* Calcula termino forzante f[i] en cada nodo */
void init_problem(JacobiContext *ctx);

/* Resuelve Poisson iterativamente (version secuencial O0) */
void jacobi_seq(JacobiContext *ctx);

/* Resuelve Poisson (version secuencial optimizada memoria O0) */
int jacobi_seq_mem(JacobiContext *ctx);

/* Resuelve Poisson en paralelo con hilos (pthread) */
int jacobi_threads(JacobiContext *ctx);

/* Resuelve Poisson en paralelo con procesos (fork + IPC) */
int jacobi_processes(JacobiContext *ctx);

#endif
