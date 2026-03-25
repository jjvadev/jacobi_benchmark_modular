#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>

static int nodes_from_k(int k) {
    if (k < 1 || k > 29) {
        return -1;
    }
    return (1 << k) + 1;
}

int main(int argc, char **argv) {
    JacobiContext ctx;
    double t0;
    double t1;
    double tolerance;
    int k;
    int n;

    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Uso: %s <k> <max_sweeps> [tol_residual_rms]\n", argv[0]);
        fprintf(stderr, "  n = 2^k + 1\n");
        return EXIT_FAILURE;
    }

    k = atoi(argv[1]);
    n = nodes_from_k(k);
    if (n < 0) {
        fprintf(stderr, "Error: k debe estar en [1, 29].\n");
        return EXIT_FAILURE;
    }

    if (init_context_heap(&ctx, n, atoi(argv[2]), 1) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto secuencial.\n");
        return EXIT_FAILURE;
    }

    tolerance = (argc == 4) ? atof(argv[3]) : 1e-6;
    if (tolerance <= 0.0) {
        fprintf(stderr, "Error: tol_residual_rms debe ser > 0.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    ctx.tolerance = tolerance;

    t0 = wall_time_seconds();
    jacobi_seq(&ctx);
    t1 = wall_time_seconds();

    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
