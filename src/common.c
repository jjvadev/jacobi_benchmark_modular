#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static double rhs_value(double x) {
    /* Fuente simple del ejemplo base. Si tu profesor exige otra, cambia esta linea. */
    return x;
}

double wall_time_seconds(void) {
    struct timeval t;
    gettimeofday(&t, NULL);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

int init_context_heap(JacobiContext *ctx, int n, int nsweeps, int workers) {
    size_t bytes;

    if (ctx == NULL || n <= 1 || nsweeps <= 0 || workers <= 0 || workers > MAX_WORKERS) {
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->n = n;
    ctx->nsweeps = nsweeps;
    ctx->workers = workers;
    ctx->h = 1.0 / (double)n;
    ctx->h2 = ctx->h * ctx->h;

    bytes = (size_t)(n + 1) * sizeof(double);
    ctx->u = (double *)malloc(bytes);
    ctx->utmp = (double *)malloc(bytes);
    ctx->f = (double *)malloc(bytes);

    if (ctx->u == NULL || ctx->utmp == NULL || ctx->f == NULL) {
        free_context_heap(ctx);
        return -1;
    }

    init_problem(ctx);
    return 0;
}

void init_problem(JacobiContext *ctx) {
    int i;
    double x;

    ctx->u[0] = 0.0;
    ctx->u[ctx->n] = 0.0;
    ctx->utmp[0] = 0.0;
    ctx->utmp[ctx->n] = 0.0;

    for (i = 1; i < ctx->n; ++i) {
        ctx->u[i] = 0.0;
        ctx->utmp[i] = 0.0;
    }

    for (i = 0; i <= ctx->n; ++i) {
        x = (double)i * ctx->h;
        ctx->f[i] = rhs_value(x);
    }
}

void free_context_heap(JacobiContext *ctx) {
    if (ctx == NULL) {
        return;
    }

    free(ctx->u);
    free(ctx->utmp);
    free(ctx->f);

    ctx->u = NULL;
    ctx->utmp = NULL;
    ctx->f = NULL;
}
