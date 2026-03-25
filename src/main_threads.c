/*
 * Programa principal de la versión paralela con threads (jacobi_threads)
 * del método de Jacobi.
 * 
 * Resuelve numéricamente la ecuación de Poisson 1D:
 *     -u''(x) = f(x)  en [0, 1]
 *     con condiciones de frontera Dirichlet: u(0) = 0, u(1) = 0
 * 
 * Paralelización con POSIX threads (pthreads):
 * - Cada thread ejecuta iteradas de Jacobi sobre su rango de nodos
 * - Sincronización con barreras: todos los threads esperan al final de cada barrida
 * - Reducción de residual: thread master agrega las sumas cuadráticas locales
 * 
 * Entrada: k, max_sweeps, num_threads, y opcionalmente tolerancia
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
 * Punto de entrada principal para la versión paralela con threads.
 * 
 * Flujo:
 *   1. Parsear argumentos: k (exponente), max_sweeps (máximas iteraciones),
 *      num_threads (cantidad de threads paralelos), y opcionalmente tolerance
 *   2. Calcular n = 2^k + 1 (número de nodos)
 *   3. Inicializar contexto: asignar memoria para u, utmp, f
 *   4. Ejecutar el algoritmo paralelo (jacobi_threads)
 *      - Crea num_threads threads que trabajan en paralelo
 *      - Cada thread actualiza su rango de nodos de forma independiente
 *      - Se sincronizan con barreras al final de cada iteración
 *   5. Reportear tiempo transcurrido
 * 
 * Argumentos:
 *   argv[1]: k (exponente, rango [1, 29])
 *   argv[2]: max_sweeps (máximo número de iteraciones)
 *   argv[3]: num_threads (número de threads paralelos a crear)
 *   argv[4]: tol_residual_rms (tolerancia, opcional, default 1e-6)
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
     * Requiere exactamente 4 o 5 argumentos (incluyendo el nombre del programa)
     * Diferencia con main_seq: aquí hay un argumento adicional num_threads
     */
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Uso: %s <k> <max_sweeps> <num_threads> [tol_residual_rms]\n", argv[0]);
        fprintf(stderr, "  nodos n = 2^k + 1\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 2: Calcular número de nodos
     * k = exponente, n = 2^k + 1
     * (Idéntico a main_seq.c)
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
     * - DIFERENCIA: el tercer argumento es num_threads (no 1 como en seq)
     */
    if (init_context_heap(&ctx, n, atoi(argv[2]), atoi(argv[3])) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto de hilos.\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 4: Parsear tolerancia (criterio de convergencia)
     * Si no se especifica, usar default 1e-6
     * (Idéntico a main_seq.c)
     */
    tolerance = (argc == 5) ? atof(argv[4]) : 1e-6;
    if (tolerance <= 0.0) {
        fprintf(stderr, "Error: tol_residual_rms debe ser > 0.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    ctx.tolerance = tolerance;

    /*
     * PASO 5: Ejecutar algoritmo paralelo y medir tiempo
     * Registra tiempo inicial, ejecuta jacobi_threads, registra tiempo final
     * 
     * jacobi_threads crea num_threads threads que:
     *   - Dividen el dominio [1, n-2] en rangos disjuntos
     *   - Actualizan sus nodos en paralelo en cada barrida
     *   - Se sincronizan con pthread_barrier al final de cada barrida
     *   - El thread 0 (master) agrega los residuales locales para convergencia
     */
    t0 = wall_time_seconds();
    if (jacobi_threads(&ctx) != 0) {
        fprintf(stderr, "Error: fallo en ejecucion con hilos.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    t1 = wall_time_seconds();

    /*
     * PASO 6: Reportear resultado
     * Imprime el tiempo transcurrido en segundos (con 6 decimales)
     * Este tiempo incluye: creación de threads, sincronizaciones, y limpieza
     */
    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
