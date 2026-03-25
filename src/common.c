#define _POSIX_C_SOURCE 200809L
/*
 * ============================================================
 *  COMMON.C - FUNCIONES COMPARTIDAS
 * ============================================================
 *  Implementacion de funciones comunes a todas las variantes:
 *  - Inicializacion del contexto y problema
 *  - Limpieza de memoria
 *  - Temporizacion
 *
 *  Estas funciones se ejecutan ANTES de llamar a cualquiera
 *  de las variantes paralelas (jacobi_seq, jacobi_threads, etc).
 * ============================================================ */

#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================
 *  rhs_value(x)
 * ============================================================
 *  Calcula el termino forzante f(x) en la ecuacion de Poisson.
 *
 *  En nuestro caso:  -u''(x) = f(x)
 *  donde f(x) es conocido (dado) y u(x) es lo que buscamos.
 *
 *  Esta funcion define el lado derecho (RHS) del sistema.
 *  En el algoritmo Jacobi, se evalua una sola vez en cada
 *  nodo (durante init_problem) y luego se reutiliza en
 *  todas las iteraciones.
 *
 * ============================================================ */
static double rhs_value(double x) {
    return x;  /* Termino forzante simple: f(x) = x */
}

/* ============================================================
 *  wall_time_seconds()
 * ============================================================
 *  Retorna el tiempo "muro" (wall clock time) actual en
 *  segundos con resolucion de nanosegundos.
 *
 *  CLOCK_MONOTONIC: reloj que nunca retrocede, ideal para
 *  medir duraciones (no afectado por cambios NTP, etc).
 *
 *  Uso en benchmark:
 *    t_inicio = wall_time_seconds();
 *    jacobi_seq(&ctx);  << ejecutar algoritmo
 *    tiempo_total = wall_time_seconds() - t_inicio;
 */
double wall_time_seconds(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* ============================================================
 *  init_context_heap(ctx, n, nsweeps, workers)
 * ============================================================
 *  INICIALIZA el contexto Jacobi: reserva memoria dinamica,
 *  valida parametros y prepara los datos para resolver.
 *
 *  PARAMETROS:
 *    ctx      - estructura a llenar
 *    n        - numero de nodos
 *    nsweeps  - iteraciones maximas
 *    workers  - numero de hilos/procesos
 *
 *  FLUJO:
 *  1. Valida parametros (n>1, workers <= MAX_WORKERS, etc)
 *  2. Limpia ctx con memset (lo pone a cero)
 *  3. Calcula parametros geometricos (h, h2)
 *  4. Reserva memoria para u[], utmp[], f[] (cada uno n elementos)
 *  5. Llama a init_problem() para llenar f[i] en cada nodo
 *
 *  MEMORIA:
 *  - u, utmp: se inicializan a cero (condicion inicial Jacobi)
 *  - f: se calcula en init_problem() basado en rhs_value(x)
 *
 *  RETORNA:
 *    0 si exito
 *    -1 si error (parametros invalidos o malloc fallo)
 *
 *  IMPORTANTE: Luego debes llamar free_context_heap(ctx)
 *  para liberar la memoria cuando termines.
 * ============================================================ */
int init_context_heap(JacobiContext *ctx, int n, int nsweeps, int workers) {
    size_t bytes;

    /* Validacion de parametros */
    if (ctx == NULL || n <= 1 || nsweeps <= 0 || workers <= 0 || workers > MAX_WORKERS) {
        return -1;
    }

    /* Limpiar estructura (poner a cero) */
    memset(ctx, 0, sizeof(*ctx));

    /* Guardar parametros del problema */
    ctx->n = n;
    ctx->nsweeps = nsweeps;
    ctx->workers = workers;
    ctx->sweeps_done = 0;

    /* Calcular parametros geometricos (precalculados para eficiencia) */
    ctx->h = 1.0 / (double)(n - 1);   /* espaciamiento: h = 1 / (n-1) */
    ctx->h2 = ctx->h * ctx->h;  /* h al cuadrado (aparece en Jacobi) */

    /* Configurar criterio de convergencia */
    ctx->tolerance = 1e-6;       /* residual RMS umbral */
    ctx->last_error = 0.0;

    /* Reservar memoria para los tres arreglos principales */
    bytes = (size_t)n * sizeof(double);
    ctx->u = (double *)malloc(bytes);      /* solucion actual */
    ctx->utmp = (double *)malloc(bytes);   /* solucion nueva (temporal) */
    ctx->f = (double *)malloc(bytes);      /* termino forzante */

    /* Verificar que malloc tuvo exito (no devolvio NULL) */
    if (ctx->u == NULL || ctx->utmp == NULL || ctx->f == NULL) {
        free_context_heap(ctx);  /* limpiar lo que SI se asigno */
        return -1;
    }

    /* Inicializar valores f[i] en cada nodo */
    init_problem(ctx);
    return 0;
}

/* ============================================================
 *  init_problem(ctx)
 * ============================================================
 *  INICIALIZAR el problema fisico:
 *  1. Poner condiciones de frontera (bordes fijos a cero)
 *  2. Inicializar u[] y utmp[] a cero (estimacion inicial Jacobi)
 *  3. Calcular f[i] en cada nodo basado en rhs_value(x_i)
 *
 *  En el metodo Jacobi, partimos de una aproximacion inicial
 *  (tipicamente u[i] = 0) y mejoramos iterativamente.
 *  El termino f[i] es CONSTANTE en todas las iteraciones.
 *
 *  Condiciones de Dirichlet (frontera fija):
 *    u[0]   = 0.0     <- frontera izquierda
 *    u[n-1] = 0.0     <- frontera derecha
 *  Estos valores NUNCA cambian durante las iteraciones.
 *
 *  PARAMETROS:
 *    ctx - contexto ya inicializado con memoria alojada
 *
 *  IMPORTANTE para paralelismo:
 *  Esta funcion se ejecuta en el proceso/hilo PRINCIPAL.
 *  f[] se computa una sola vez y luego es SOLO LECTURA
 *  en todas las iteraciones (no hay race conditions).
 * ============================================================ */
void init_problem(JacobiContext *ctx) {
    int i;
    double x;

    /* Establecer condiciones de frontera (Dirichlet) */
    ctx->u[0] = 0.0;
    ctx->u[ctx->n - 1] = 0.0;
    ctx->utmp[0] = 0.0;
    ctx->utmp[ctx->n - 1] = 0.0;

    /* Inicializar u[] y utmp[] a cero (estimacion inicial) */
    for (i = 1; i < ctx->n - 1; ++i) {
        ctx->u[i] = 0.0;
        ctx->utmp[i] = 0.0;
    }

    /* Calcular termino forzante f[i] en cada nodo */
    for (i = 0; i < ctx->n; ++i) {
        x = (double)i * ctx->h;    /* posicion fisica del nodo: x_i = i * h */
        ctx->f[i] = rhs_value(x);  /* evaluar f(x_i) */
    }
}

/* ============================================================
 *  free_context_heap(ctx)
 * ============================================================
 *  LIBERAR toda la memoria dinamica reservada por
 *  init_context_heap().
 *
 *  Llamar esta funcion cuando termines con el contexto,
 *  preferentemente al final de main().
 *
 *  PARAMETROS:
 *    ctx - contexto a liberar
 *
 *  ACCIONES:
 *    - Llama free() a u, utmp, f
 *    - Pone punteros a NULL (buena practica, evita
 *      dereferencias accidentales de punteros liberados)
 *
 *  NOTA: No libera ctx mismo (asumimos que vive en la
 *  pila o es responsabilidad del llamador).
 * ============================================================ */
void free_context_heap(JacobiContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    /* Liberar los tres arreglos dinamicos */
    free(ctx->u);
    free(ctx->utmp);
    free(ctx->f);

    /* Poner punteros a NULL (evita dereferencias accidentales) */
    ctx->u = NULL;
    ctx->utmp = NULL;
    ctx->f = NULL;
}
