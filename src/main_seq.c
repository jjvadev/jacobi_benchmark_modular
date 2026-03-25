/*
 * Programa principal de la versión secuencial (jacobi_seq) del método de Jacobi.
 * 
 * Resuelve numéricamente la ecuación de Poisson 1D:
 *     -u''(x) = f(x)  en [0, 1]
 *     con condiciones de frontera Dirichlet: u(0) = 0, u(1) = 0
 * 
 * El algoritmo discretiza el dominio [0, 1] en n nodos uniformemente espaciados
 * con h = 1/(n-1), y aplica iteradas de Jacobi hasta convergencia.
 * 
 * Entrada: k, max_sweeps, y opcionalmente tolerancia
 * Salida: tiempo de ejecución en segundos
 */

#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>

/*
 * Calcula el número de nodos (n) a partir del exponente k.
 * 
 * Siguiendo la convención del PDF de Burkardt:
 *     n = 2^k + 1  (número de nodos en el dominio discretizado)
 * 
 * Ejemplo: si k=5, entonces n = 32 + 1 = 33 nodos
 * 
 * Parámetros:
 *   k: exponente (debe estar en [1, 29])
 * 
 * Retorna:
 *   n = 2^k + 1 si k es válido, o -1 si está fuera de rango
 */
static int nodes_from_k(int k) {
    if (k < 1 || k > 29) {
        return -1;
    }
    return (1 << k) + 1;  /* 1 << k es equivalente a 2^k en potencias de 2 */
}


/*
 * Punto de entrada principal.
 * 
 * Flujo:
 *   1. Parsear argumentos: k (exponente), max_sweeps (máximas iteraciones),
 *      y opcionalmente tolerance (criterio de convergencia)
 *   2. Calcular n = 2^k + 1 (número de nodos)
 *   3. Inicializar contexto: asignar memoria para u, utmp, f
 *   4. Ejecutar el algoritmo iterativo (jacobi_seq)
 *   5. Reportear tiempo transcurrido
 * 
 * Argumentos:
 *   argv[1]: k (exponente, rango [1, 29])
 *   argv[2]: max_sweeps (máximo número de iteraciones)
 *   argv[3]: tol_residual_rms (tolerancia, opcional, default 1e-6)
 * 
 * Salida:
 *   Un número real: tiempo de ejecución en segundos
 */
int main(int argc, char **argv) {
    JacobiContext ctx;
    double t0;
    double t1;
    double tolerance;
    int k;
    int n;

    /*
     * PASO 1: Validación de argumentos
     * Requiere exactamente 3 o 4 argumentos (incluyendo el nombre del programa)
     */
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Uso: %s <k> <max_sweeps> [tol_residual_rms]\n", argv[0]);
        fprintf(stderr, "  nodos n = 2^k + 1\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 2: Calcular número de nodos
     * k = exponente, n = 2^k + 1
     */
    k = atoi(argv[1]);
    n = nodes_from_k(k);
    if (n < 0) {
        fprintf(stderr, "Error: k debe estar en [1, 29].\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 3: Inicializar contexto Jacobi
     * - Asigna memoria para u[], utmp[], f[]
     * - Calcula h = 1/(n-1) (espaciamiento uniforme)
     * - Establece condiciones de frontera u[0] = u[n-1] = 0
     * - Inicializa f[i] (término fuente)
     */
    if (init_context_heap(&ctx, n, atoi(argv[2]), 1) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto secuencial.\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 4: Parsear tolerancia (criterio de convergencia)
     * Si no se especifica, usar default 1e-6
     */
    tolerance = (argc == 4) ? atof(argv[3]) : 1e-6;
    if (tolerance <= 0.0) {
        fprintf(stderr, "Error: tol_residual_rms debe ser > 0.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    ctx.tolerance = tolerance;

    /*
     * PASO 5: Ejecutar algo ritmo y medir tiempo
     * Registra tiempo inicial, ejecuta Jacobi, registra tiempo final
     */
    t0 = wall_time_seconds();
    jacobi_seq(&ctx);
    t1 = wall_time_seconds();

    /*
     * PASO 6: Reportear resultado
     * Imprime el tiempo transcurrido en segundos (con 6 decimales)
     */
    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
