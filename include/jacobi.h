#ifndef JACOBI_H
#define JACOBI_H

#include <stddef.h>

#define MAX_WORKERS 32

typedef struct {
    int n;              /* numero de subintervalos, puntos = n + 1 */
    int nsweeps;        /* iteraciones Jacobi */
    int workers;        /* hilos o procesos */
    double h;
    double h2;
    double *u;
    double *utmp;
    double *f;
} JacobiContext;

double wall_time_seconds(void);
int init_context_heap(JacobiContext *ctx, int n, int nsweeps, int workers);
void free_context_heap(JacobiContext *ctx);
void init_problem(JacobiContext *ctx);
void jacobi_seq(JacobiContext *ctx);
int jacobi_threads(JacobiContext *ctx);
int jacobi_processes(JacobiContext *ctx);

#endif
