/*
 * Programa principal de la versión paralela con procesos (jacobi_processes)
 * del método de Jacobi.
 * 
 * Resuelve numéricamente la ecuación de Poisson 1D:
 *     -u''(x) = f(x)  en [0, 1]
 *     con condiciones de frontera Dirichlet: u(0) = 0, u(1) = 0
 * 
 * Paralelización con procesos (fork):
 * - Proceso padre crea num_processes procesos hijo usando fork()
 * - Comunicación inter-proceso mediante:
 *   * Memoria compartida (mmap): arrays de solución
 *   * Pipes: comandos de sincronización entre procesos
 * - El padre orquesta las iteraciones y reduce residuales
 * - Los hijos ejecutan cálculos locales de forma independiente
 * 
 * Entrada: k, max_sweeps, num_processes, y opcionalmente tolerancia
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
 * Punto de entrada principal para la versión paralela con procesos.
 * 
 * Flujo:
 *   1. Parsear argumentos: k (exponente), max_sweeps (máximas iteraciones),
 *      num_processes (cantidad de procesos paralelos), y opcionalmente tolerance
 *   2. Calcular n = 2^k + 1 (número de nodos)
 *   3. Inicializar contexto: asignar memoria para u, utmp, f
 *   4. Ejecutar el algoritmo paralelo (jacobi_processes)
 *      - Crea num_processes procesos hijo con fork()
 *      - Usa memoria compartida (mmap) para los arrays u, utmp, f
 *      - Usa pipes para sincronización: procesos hijo esperan comandos del padre
 *      - El padre orquesta las iteraciones y reduce residuales
 *   5. Reportear tiempo transcurrido
 * 
 * Argumentos:
 *   argv[1]: k (exponente, rango [1, 29])
 *   argv[2]: max_sweeps (máximo número de iteraciones)
 *   argv[3]: num_processes (número de procesos paralelos a crear)
 *   argv[4]: tol_residual_rms (tolerancia, opcional, default 1e-6)
 * 
 * Salida:
 *   Un número real: tiempo de ejecución en segundos
 * 
 * Nota: Los procesos hijo NO comparten el heap del padre (cada uno tiene
 * su propio espacio de memoria). Comunicación solo mediante:
 *   - Memoria compartida explícita (mmap)
 *   - Pipes (para sincronización ordinal)
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
     * Similar a main_threads.c pero con "num_procs" en lugar de "num_threads"
     */
    if (argc != 4 && argc != 5) {
        fprintf(stderr, "Uso: %s <k> <max_sweeps> <num_procs> [tol_residual_rms]\n", argv[0]);
        fprintf(stderr, "  nodos n = 2^k + 1\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 2: Calcular número de nodos
     * k = exponente, n = 2^k + 1
     * (Idéntico a main_threads.c y main_seq.c)
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
     * - El tercer argumento es num_processes (número de procesos a crear)
     */
    if (init_context_heap(&ctx, n, atoi(argv[2]), atoi(argv[3])) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar el contexto de procesos.\n");
        return EXIT_FAILURE;
    }

    /*
     * PASO 4: Parsear tolerancia (criterio de convergencia)
     * Si no se especifica, usar default 1e-6
     * (Idéntico a las versiones anteriores)
     */
    tolerance = (argc == 5) ? atof(argv[4]) : 1e-6;
    if (tolerance <= 0.0) {
        fprintf(stderr, "Error: tol_residual_rms debe ser > 0.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    ctx.tolerance = tolerance;

    /*
     * PASO 5: Ejecutar algoritmo paralelo con procesos y medir tiempo
     * Registra tiempo inicial, ejecuta jacobi_processes, registra tiempo final
     * 
     * jacobi_processes realiza:
     *   - Crea memoria compartida (mmap) para u, utmp, f
     *   - Copia datos iniciales a memoria compartida
     *   - fork() num_processes veces para crear procesos hijo
     *   - Cada hijo entra en un loop esperando comandos del padre
     *   - El padre (proceso 0) orquesta ctdas las iteraciones:
     *     * Envía comandos 'A' o 'B' por pipes (alternancia par/impar)
     *     * Recibe reconocimientos de cada hijo
     *     * Calcula residual global
     *     * Verifica convergencia
     *   - El padre envía comando 'Q' (quit) para terminar los hijos
     *   - Espera con waitpid() a que todos los hijos terminen
     */
    t0 = wall_time_seconds();
    if (jacobi_processes(&ctx) != 0) {
        fprintf(stderr, "Error: fallo en ejecucion con procesos.\n");
        free_context_heap(&ctx);
        return EXIT_FAILURE;
    }
    t1 = wall_time_seconds();

    /*
     * PASO 6: Reportear resultado
     * Imprime el tiempo transcurrido en segundos (con 6 decimales)
     * Este tiempo incluye: creación de procesos, comunicación por pipes,
     * sincronización, y limpieza
     */
    printf("%.6f\n", t1 - t0);
    free_context_heap(&ctx);
    return EXIT_SUCCESS;
}
