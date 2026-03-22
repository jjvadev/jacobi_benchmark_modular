#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    JacobiContext ctx;
    double t0;
    double t1;

    if (argc != 4) {
        fprintf(stderr, "Uso: %s <n> <nsweeps> <num_procs>\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (init_context_heap(&ctx, atoi(argv[1]), atoi(argv[2]), atoi(argv[3])) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto de procesos.\n");
        return EXIT_FAILURE;
    }

    t0 = wall_time_seconds();
    if (jacobi_processes(&ctx) != 0) {
        fprintf(stderr, "Error: fallo en ejecucion con procesos.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    t1 = wall_time_seconds();

    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
