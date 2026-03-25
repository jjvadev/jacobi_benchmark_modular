#include "jacobi.h"

#include <math.h>
#include <stdlib.h>

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

    /* Precompute h2*f[i] once to reduce repeated work and memory traffic in the inner loop. */
    scaled_rhs = (double *)malloc((size_t)(ctx->n + 1) * sizeof(double));
    if (scaled_rhs == NULL) {
        return -1;
    }

    scaled_rhs[0] = 0.0;
    scaled_rhs[ctx->n] = 0.0;
    for (i = 1; i < ctx->n; ++i) {
        scaled_rhs[i] = ctx->h2 * ctx->f[i];
    }

    a = ctx->u;
    b = ctx->utmp;
    g = scaled_rhs;

    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        double *restrict dst = b;
        const double *restrict src = a;
        const double *restrict rhs = g;
        double *tmp;
        double residual_sq_sum = 0.0;

        for (i = 1; i < ctx->n; ++i) {
            dst[i] = 0.5 * (src[i - 1] + src[i + 1] + rhs[i]);
        }

        for (i = 1; i < ctx->n; ++i) {
            double ri = (-dst[i - 1] + 2.0 * dst[i] - dst[i + 1]) / ctx->h2 - ctx->f[i];
            residual_sq_sum += ri * ri;
        }

        tmp = a;
        a = b;
        b = tmp;

        sweeps_done = sweep + 1;
        last_residual_rms = sqrt(residual_sq_sum / (double)(ctx->n + 1));
        if (last_residual_rms <= ctx->tolerance) {
            break;
        }
    }

    if (a != ctx->u) {
        for (i = 1; i < ctx->n; ++i) {
            ctx->u[i] = a[i];
        }
    }

    ctx->sweeps_done = sweeps_done;
    ctx->last_error = last_residual_rms;

    free(scaled_rhs);
    return 0;
}
