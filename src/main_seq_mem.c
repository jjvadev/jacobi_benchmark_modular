#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    JacobiContext ctx;
    double t0;
    double t1;

    if (argc != 3) {
        fprintf(stderr, "Uso: %s <n> <nsweeps>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (init_context_heap(&ctx, atoi(argv[1]), atoi(argv[2]), 1) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto secuencial con optimizacion de memoria.\n");
        return EXIT_FAILURE;
    }

    t0 = wall_time_seconds();
    if (jacobi_seq_mem(&ctx) != 0) {
        fprintf(stderr, "Error: fallo en ejecucion secuencial con optimizacion de memoria.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    t1 = wall_time_seconds();

    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
