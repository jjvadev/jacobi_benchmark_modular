/*
 * Programa principal de la version secuencial optimizada en memoria
 * (jacobi_seq_mem) del metodo de Jacobi.
 *
 * Resuelve numericamente la ecuacion de Poisson 1D:
 *     -u''(x) = f(x)  en [0, 1]
 * con condiciones de frontera Dirichlet: u(0) = 0, u(1) = 0
 *
 * Esta variante mantiene el mismo resultado matematico que la secuencial base,
 * pero optimiza trafico de memoria y trabajo repetido dentro del kernel.
 *
 * Entrada: k, max_sweeps, y opcionalmente tolerancia
 * Salida: tiempo de ejecucion en segundos
 */

#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Calcula el numero de nodos (n) a partir del exponente k.
 *
 * Convencion usada en el proyecto:
 *   n = 2^k + 1 (nodos totales de la malla)
 */
static int nodes_from_k(int k) {
    if (k < 1 || k > 29) {
        return -1;
    }
    return (1 << k) + 1;
}

/*
 * Punto de entrada para la variante secuencial con optimizacion de memoria.
 *
 * Flujo:
 *   1. Validar argumentos
 *   2. Convertir k a n (nodos)
 *   3. Inicializar contexto (u, utmp, f, parametros)
 *   4. Configurar tolerancia de convergencia
 *   5. Ejecutar jacobi_seq_mem y medir tiempo
 *   6. Imprimir tiempo y liberar recursos
 */
int main(int argc, char **argv) {
    JacobiContext ctx;
    double t0;
    double t1;
    double tolerance;
    int k;
    int n;

    /* Requiere: k max_sweeps [tol_residual_rms] */
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Uso: %s <k> <max_sweeps> [tol_residual_rms]\n", argv[0]);
        fprintf(stderr, "  nodos n = 2^k + 1\n");
        return EXIT_FAILURE;
    }

    /* k define tamano de malla: n = 2^k + 1 */
    k = atoi(argv[1]);
    n = nodes_from_k(k);
    if (n < 0) {
        fprintf(stderr, "Error: k debe estar en [1, 29].\n");
        return EXIT_FAILURE;
    }

    /* workers=1 porque sigue siendo version secuencial */
    if (init_context_heap(&ctx, n, atoi(argv[2]), 1) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto secuencial con optimizacion de memoria.\n");
        return EXIT_FAILURE;
    }

    /* Tolerancia por defecto si no se pasa por linea de comandos */
    tolerance = (argc == 4) ? atof(argv[3]) : 1e-6;
    if (tolerance <= 0.0) {
        fprintf(stderr, "Error: tol_residual_rms debe ser > 0.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    ctx.tolerance = tolerance;

    /* Medicion de tiempo de la fase de computo del solver */
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
