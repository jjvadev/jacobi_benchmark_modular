#include "jacobi.h"

void jacobi_seq(JacobiContext *ctx) {
    int sweep;
    int i;

    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        if ((sweep & 1) == 0) {
            for (i = 1; i < ctx->n; ++i) {
                ctx->utmp[i] = 0.5 * (ctx->u[i - 1] + ctx->u[i + 1] + ctx->h2 * ctx->f[i]);
            }
        } else {
            for (i = 1; i < ctx->n; ++i) {
                ctx->u[i] = 0.5 * (ctx->utmp[i - 1] + ctx->utmp[i + 1] + ctx->h2 * ctx->f[i]);
            }
        }
    }

    /* Si nsweeps es impar, la solucion final queda en utmp. La copiamos a u. */
    if ((ctx->nsweeps & 1) != 0) {
        for (i = 1; i < ctx->n; ++i) {
            ctx->u[i] = ctx->utmp[i];
        }
    }
}
