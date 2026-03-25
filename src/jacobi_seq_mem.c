#include "jacobi.h"

#include <math.h>
#include <stdlib.h>

/*
 * Version secuencial optimizada por memoria/cache del metodo de Jacobi.
 *
 * Que resuelve:
 *   -u''(x) = f(x) en [0,1], con u(0)=u(1)=0.
 *
 * Diferencia clave vs jacobi_seq (secuencial normal):
 * 1) Precomputa h2*f[i] una sola vez en scaled_rhs[].
 *    - Evita recalcular ese producto en cada barrida.
 *    - Reduce trafico de lectura de f y operaciones en el loop interno.
 * 2) Usa punteros a/b y swap de punteros por barrida.
 *    - Evita copiar arreglo completo en cada iteracion.
 *    - Mantiene el esquema de doble buffer de Jacobi (src -> dst).
 * 3) Usa punteros restrict en el kernel.
 *    - Ayuda al compilador a vectorizar y optimizar accesos.
 */
int jacobi_seq_mem(JacobiContext *ctx) {
    double *scaled_rhs;
    double *a;
    double *b;
    const double *g;
    int i;
    int sweep;
    int sweeps_done = 0;
    double last_residual_rms = 0.0;

    if (ctx == NULL) {
        return -1;
    }

    /* Optimizacion 1: precomputar h2*f[i] una sola vez. */
    scaled_rhs = (double *)malloc((size_t)ctx->n * sizeof(double));
    if (scaled_rhs == NULL) {
        return -1;
    }

    scaled_rhs[0] = 0.0;
    scaled_rhs[ctx->n - 1] = 0.0;
    for (i = 1; i < ctx->n - 1; ++i) {
        scaled_rhs[i] = ctx->h2 * ctx->f[i];
    }

    /*
     * a = buffer fuente (lectura), b = buffer destino (escritura).
     * g apunta al termino precomputado h2*f[i].
     */
    a = ctx->u;
    b = ctx->utmp;
    g = scaled_rhs;

    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        /* Optimizacion 2: restrict para facilitar optimizacion del compilador. */
        double *restrict dst = b;
        const double *restrict src = a;
        const double *restrict rhs = g;
        double *tmp;
        double residual_sq_sum = 0.0;

        for (i = 1; i < ctx->n - 1; ++i) {
            dst[i] = 0.5 * (src[i - 1] + src[i + 1] + rhs[i]);
        }

        for (i = 1; i < ctx->n - 1; ++i) {
            double ri = (-dst[i - 1] + 2.0 * dst[i] - dst[i + 1]) / ctx->h2 - ctx->f[i];
            residual_sq_sum += ri * ri;
        }

        /* Optimizacion 3: swap de punteros en lugar de copiar arreglos completos. */
        tmp = a;
        a = b;
        b = tmp;

        sweeps_done = sweep + 1;
        last_residual_rms = sqrt(residual_sq_sum / (double)ctx->n);
        if (last_residual_rms <= ctx->tolerance) {
            break;
        }
    }

    /* Si el resultado final quedo en utmp (a != ctx->u), copiar solo interior. */
    if (a != ctx->u) {
        for (i = 1; i < ctx->n - 1; ++i) {
            ctx->u[i] = a[i];
        }
    }

    ctx->sweeps_done = sweeps_done;
    ctx->last_error = last_residual_rms;

    free(scaled_rhs);
    return 0;
}
