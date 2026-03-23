#include "jacobi.h"

#include <stdlib.h>

int jacobi_seq_mem(JacobiContext *ctx) {
    double *scaled_rhs;
    double *a;
    double *b;
    const double *g;
    int i;
    int sweep;

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

        for (i = 1; i < ctx->n; ++i) {
            dst[i] = 0.5 * (src[i - 1] + src[i + 1] + rhs[i]);
        }

        tmp = a;
        a = b;
        b = tmp;
    }

    if (a != ctx->u) {
        for (i = 1; i < ctx->n; ++i) {
            ctx->u[i] = a[i];
        }
    }

    free(scaled_rhs);
    return 0;
}
