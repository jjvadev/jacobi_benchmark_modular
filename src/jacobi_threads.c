/*
 * jacobi_threads.c: Implementación paralela del método de Jacobi usando POSIX threads.
 * 
 * ============================================================================
 * PROBLEMA RESUELTO
 * ============================================================================
 * Resuelve numéricamente la ecuación de Poisson 1D:
 *     -u''(x) = f(x)  en el intervalo [0, 1]
 *     con condiciones de frontera Dirichlet: u(0) = 0, u(1) = 0
 * 
 * La solución se aproxima usando el método iterativo de Jacobi en una malla
 * uniforme de n nodos discretos.
 * 
 * ============================================================================
 * ESTRATEGIA DE PARALELIZACIÓN
 * ============================================================================
 * 
 * En lugar de un único thread que ejecuta todas las iteraciones secuencialmente,
 * se crean múltiples threads que trabajan en paralelo sobre diferentes regiones
 * del dominio:
 * 
 *   main_thread (thread 0)   - Crea threads, sincroniza, reduce residuales
 *   worker_thread 1          - Actualiza nodos [s1, e1)
 *   worker_thread 2          - Actualiza nodos [s2, e2)
 *   ...
 *   worker_thread k          - Actualiza nodos [sk, ek)
 * 
 * Partición del dominio interior [1, n-2]:
 *   - Cada thread obtiene un subconjunto disjunto de nodos
 *   - La partición es equilibrada: cada thread procesa ~(n-2)/workers nodos
 *   - Los threads trabajan de forma independiente DURANTE la iteración
 *   - Se sincronizan al final de cada iteración con pthread_barrier
 * 
 * ============================================================================
 * SINCRONIZACIÓN: BARRERA (pthread_barrier)
 * ============================================================================
 * 
 * La barrera garantiza que todos los threads terminen su trabajo antes de:
 *   1. Que el thread master agregue los residuales locales
 *   2. Que se evalúe el criterio de convergencia
 *   3. Que comience la siguiente iteración
 * 
 * Sin la barrera, un thread rápido podría re-calcular sus nodos antes de que
 * otro thread haya terminado la iteración anterior, rompiendo la consistencia.
 * 
 * ============================================================================
 * ESTRATEGIA PAR/IMPAR (Double Buffering)
 * ============================================================================
 * 
 * En cada iteración (sweep), el método Jacobi requiere computar todos los
 * valores nuevos u_new[i] basados en valores viejos u_old[i]:
 *   u_new[i] = 0.5 * (u_old[i-1] + u_old[i+1] + h²*f[i])
 * 
 * Con múltiples threads actualizando simultáneamente u y utmp, podrían surgir
 * race conditions o inconsistencias de lectura/escritura.
 * 
 * Solución: ALTERNANCIA POR NÚMERO DE BARRIDA (par vs impar)
 *   - Iteraciones PARES (sweep 0, 2, 4, ...):  lee desde u, escribe en utmp
 *   - Iteraciones IMPARES (sweep 1, 3, 5, ...): lee desde utmp, escribe en u
 * 
 * Esto garantiza que:
 *   1. Cada thread lee datos SOLO de un buffer (sin ver escrituras paralelas)
 *   2. Cada thread escribe SOLO en el otro buffer (sin conflictos)
 *   3. Al final, los datos están siempre lógicamente en u (con posibles copias
 *      al final si quedó con número impar de iteraciones)
 * 
 * Diagrama de dos iteraciones:
 *   SWEEP 0 (par):  todos leen u →  todos escriben utmp  →  barrier
 *   SWEEP 1 (impar): todos leen utmp → todos escriben u   →  barrier
 *   SWEEP 2 (par):  todos leen u →  todos escriben utmp  →  barrier
 *   ...
 */

#include "jacobi.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * ThreadTask: estructura que encapsula toda la información que cada worker
 * thread necesita para ejecutar su trabajo de forma independiente.
 * 
 * Campos:
 *   ctx                    - contexto Jacobi compartido (dominio, tolerancia, etc)
 *   tid                    - identificador del thread (0 a workers-1)
 *   start, end             - rango de nodos [start, end) que actualiza este thread
 *   workers                - número total de threads (replicado para facilidad)
 *   barrier                - puntero a barrera compartida para sincronización
 *   local_residual_sq_sums - array compartido donde cada thread almacena su suma local
 *   stop_flag              - bandera compartida: =1 si convergió (solo tid=0 la establece)
 *   sweeps_done            - contador compartido de iteraciones realizadas
 *   global_residual_rms    - residual RMS global calculado por tid=0
 */
typedef struct {
    JacobiContext *ctx;
    int tid;
    int start;
    int end;
    int workers;
    pthread_barrier_t *barrier;
    double *local_residual_sq_sums;
    int *stop_flag;
    int *sweeps_done;
    double *global_residual_rms;
} ThreadTask;

/*
 * split_range: Divide el dominio interior [1, n-2] equitativamente entre workers.
 * 
 * El dominio interior tiene (n-2) nodos. Este procedimiento particiona ese rango
 * en subrangos disjuntos lo más balanceados posible.
 * 
 * Estrategia de load balancing:
 *   - Cada worker obtiene base = (n-2) / workers nodos (división entera)
 *   - Los primeros (extra) workers obtienen 1 nodo adicional
 *   - ejemplo: n=10 (interior=8), workers=3
 *     → base=2, extra=2
 *     → worker 0: 3 nodos, worker 1: 3 nodos, worker 2: 2 nodos
 * 
 * Parámetros:
 *   n         - número total de nodos (incluyendo fronteras en 0 y n-1)
 *   workers   - número de threads
 *   tid       - identificador del thread (0 a workers-1)
 *   start, end - punteros donde se escriben los límites [start, end)
 * 
 * Nota: El rango retornado es [start, end) con índices globales del dominio.
 */
static void split_range(int n, int workers, int tid, int *start, int *end) {
    int interior = n - 2;        /* cantidad de nodos interiores */
    int base = interior / workers;    /* nodos base que cada worker obtiene */
    int extra = interior % workers;   /* workers que obtienen un nodo extra */
    int count = base + (tid < extra ? 1 : 0);  /* nodos para este thread */
    int offset = tid * base + (tid < extra ? tid : extra);  /* offset global */

    *start = 1 + offset;    /* primer nodo del rango (1 porque saltamos frontera) */
    *end = *start + count;  /* un más allá del último (estilo C: [start, end)) */
}


/*
 * thread_worker: Función principal ejecutada por cada worker thread.
 * 
 * Este thread trabaja de forma INDEPENDIENTE durante el cómputo de cada iteración,
 * pero se SINCRONIZA con los demás threads al final de cada iteración.
 * 
 * Flujo de ejecución:
 * 
 *   FOR cada iteración (sweep = 0 a nsweeps-1):
 *     ┌─ FASE PARALELA: Cada thread trabaja sin coordinación
 *     │
 *     │  1. Si la iteración es PAR (sweep & 1 == 0):
 *     │     - Lee desde ctx->u (valores anteriores)
 *     │     - Escribe en ctx->utmp (nuevos valores)
 *     │
 *     │  2. Si la iteración es IMPAR (sweep & 1 == 1):
 *     │     - Lee desde ctx->utmp (valores anteriores)
 *     │     - Escribe en ctx->u (nuevos valores)
 *     │
 *     │  3. En paralelo, calcula residuales locales:
 *     │     ri = (-u[i-1] + 2*u[i] - u[i+1])/h² - f[i]
 *     │     local_sq_sum += ri * ri
 *     │
 *     └─ SINCRONIZACIÓN CON BARRERA
 *        - pthread_barrier_wait(): todos los threads se detienen aquí
 *        - El thread 0 (master) agrega las sumas locales
 *        - El thread 0 calcula residual global RMS y verifica convergencia
 *        - Todos esperan nuevamente en la barrera (segunda espera)
 *        - Los threads rápidos no pueden avanzar a la siguiente iteración
 *          hasta que el master haya evaluado stop_flag
 * 
 * El patrón BARRERA-COMPUTAR-BARRERA garantiza:
 *   ✓ Atomicidad: una iteración completa antes de la siguiente
 *   ✓ Consistencia: todos ven los mismos datos en el mismo punto
 *   ✓ Fairness: ningún thread se adelanta
 */
static void *thread_worker(void *arg) {
    ThreadTask *task = (ThreadTask *)arg;
    JacobiContext *ctx = task->ctx;
    int sweep;
    int i;

    /*
     * Loop principal de iteraciones. Cada thread ejecuta este mismo loop,
     * pero sobre su propio rango [start, end).
     */
    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        double local_residual_sq_sum = 0.0;

        /*
         * IDENTIFICAR PARIDAD DE LA ITERACIÓN ACTUAL
         * 
         * (sweep & 1) == 0  ⟹ iteración PARA: lee u, escribe utmp
         * (sweep & 1) == 1  ⟹ iteración IMPAR: lee utmp, escribe u
         */
        if ((sweep & 1) == 0) {
            /*
             * ITERACIÓN PAR (sweep = 0, 2, 4, ...):
             * Lee del buffer antiguo (ctx->u), escribe en el nuevo (ctx->utmp)
             * 
             * Actualización Jacobi:
             *   u_new[i] = 0.5 * (u_old[i-1] + u_old[i+1] + h²*f[i])
             * 
             * En este caso:
             *   utmp[i] = 0.5 * (u[i-1] + u[i+1] + h²*f[i])
             */
            for (i = task->start; i < task->end; ++i) {
                ctx->utmp[i] = 0.5 * (ctx->u[i - 1] + ctx->u[i + 1] + ctx->h2 * ctx->f[i]);
            }

            /*
             * CÁLCULO DE RESIDUAL LOCAL
             * 
             * El residual mide qué tan bien se satisface la ecuación discretizada
             * en cada nodo:
             *   r_i = (-u[i-1] + 2*u[i] - u[i+1])/h² - f[i]
             * 
             * Nota: En iteración par, el valor nuevo está en utmp[i]
             */
            for (i = task->start; i < task->end; ++i) {
                double ri = (-ctx->utmp[i - 1] + 2.0 * ctx->utmp[i] - ctx->utmp[i + 1]) / ctx->h2 - ctx->f[i];
                local_residual_sq_sum += ri * ri;
            }
        } else {
            /*
             * ITERACIÓN IMPAR (sweep = 1, 3, 5, ...):
             * Lee del buffer antiguo (ctx->utmp), escribe en el nuevo (ctx->u)
             * 
             * Actualización Jacobi:
             *   u[i] = 0.5 * (utmp[i-1] + utmp[i+1] + h²*f[i])
             */
            for (i = task->start; i < task->end; ++i) {
                ctx->u[i] = 0.5 * (ctx->utmp[i - 1] + ctx->utmp[i + 1] + ctx->h2 * ctx->f[i]);
            }

            /*
             * CÁLCULO DE RESIDUAL LOCAL
             * 
             * Ahora el valor nuevo está en u[i]
             */
            for (i = task->start; i < task->end; ++i) {
                double ri = (-ctx->u[i - 1] + 2.0 * ctx->u[i] - ctx->u[i + 1]) / ctx->h2 - ctx->f[i];
                local_residual_sq_sum += ri * ri;
            }
        }

        /*
         * PASO 1: SINCRONIZACIÓN CON BARRERA
         * 
         * Almacena la suma local de residuales al cuadrado en el array compartido.
         * Luego, espera en la barrera hasta que TODOS los threads hayan llegado
         * a este punto.
         * 
         * pthread_barrier_wait() retorna a TODOS los threads simultáneamente
         * después de que el últi mo thread llegue a la barrera.
         */
        task->local_residual_sq_sums[task->tid] = local_residual_sq_sum;
        pthread_barrier_wait(task->barrier);

        /*
         * REDUCCIÓN DE RESIDUAL POR THREAD 0
         * 
         * Solo el thread master (tid == 0) realiza la REDUCCIÓN GLOBAL:
         *   1. Suma todos los residuales locales
         *   2. Calcula el RMS global: sqrt(sum / n)
         *   3. Evalúa criterio de convergencia: ¿RMS <= tolerancia?
         *   4. Establece stop_flag para informar a los demás threads
         * 
         * Los otros threads podrían esperar, jugar... pero solo el tid=0
         * puede modificar stop_flag.
         */
        if (task->tid == 0) {
            double global_sq_sum = 0.0;
            int t;

            /* Suma todas las contribuciones locales */
            for (t = 0; t < task->workers; ++t) {
                global_sq_sum += task->local_residual_sq_sums[t];
            }

            /* Calcula RMS normalizado por número de nodos */
            *task->global_residual_rms = sqrt(global_sq_sum / (double)ctx->n);
            
            /* Actualiza contador de iteraciones completadas */
            *task->sweeps_done = sweep + 1;
            
            /* Verifica convergencia: ¿residual RMS ≤ tolerancia? */
            *task->stop_flag = (*task->global_residual_rms <= ctx->tolerance) ? 1 : 0;
        }

        /*
         * PASO 2: SEGUNDA SINCRONIZACIÓN CON BARRERA
         * 
         * Los threads que no son el master esperan aquí a que el master
         * haya terminado la reducción. Así, TODOS ven el mismo valor
         * de stop_flag antes de verificarlo.
         */
        pthread_barrier_wait(task->barrier);
        
        /*
         * VERIFICACIÓN DE CONVERGENCIA
         * 
         * Si se alcanzó la tolerancia (stop_flag != 0), salir del loop
         * de iteraciones. Todos los threads se detienen simultáneamente.
         */
        if (*task->stop_flag != 0) {
            break;
        }
    }

    return NULL;
}


/*
 * jacobi_threads: Función principal que coordina la ejecución paralela.
 * 
 * ============================================================================
 * FLUJO DE EJECUCIÓN GENERAL
 * ============================================================================
 * 
 * Este procedimiento crea múltiples threads worker que ejecutan el método
 * de Jacobi en paralelo. El thread main espera a que todos terminen antes
 * de retornar.
 * 
 * Esquema temporal:
 * 
 *   main thread:
 *   ├─ aloca memoria para threads[], tasks[], barriers
 *   ├─ inicializa barrera (ctx->workers)
 *   ├─ crea ctx->workers threads worker
 *   ├─ ESPERA (bloquea) a que todos terminen con pthread_join()
 *   └─ libera memoria y retorna
 * 
 *   worker thread i:
 *   ├─ ejecuta loop de iteraciones (thread_worker)
 *   ├─ al final o por convergencia, retorna NULL
 *   └─ termina su ejecución
 */
int jacobi_threads(JacobiContext *ctx) {
    pthread_t *threads;
    ThreadTask *tasks;
    pthread_barrier_t barrier;
    double *local_residual_sq_sums = NULL;
    double global_residual_rms = 0.0;
    int stop_flag = 0;
    int sweeps_done = 0;
    int t;
    int i;

    if (ctx == NULL) {
        return -1;
    }

    /*
     * PASO 1: ALOCACIÓN DE MEMORIA
     * 
     * Reserva espacio para:
     *   - ctx->workers pthread_t (descriptores de threads)
     *   - ctx->workers ThreadTask (parámetros de cada thread)
     *   - ctx->workers double (para que cada thread almacene su suma de residuales)
     */
    threads = (pthread_t *)malloc((size_t)ctx->workers * sizeof(pthread_t));
    tasks = (ThreadTask *)malloc((size_t)ctx->workers * sizeof(ThreadTask));
    local_residual_sq_sums = (double *)malloc((size_t)ctx->workers * sizeof(double));
    if (threads == NULL || tasks == NULL || local_residual_sq_sums == NULL) {
        free(threads);
        free(tasks);
        free(local_residual_sq_sums);
        return -1;
    }

    /*
     * PASO 2: INICIALIZAR BARRERA DE SINCRONIZACIÓN
     * 
     * pthread_barrier_init(barrera, atributos, count)
     *   - barrera: puntero a la estructura barrier
     *   - atributos: NULL (usar defaults)
     *   - count: número de threads que deben esperar en la barrera
     * 
     * La barrera asegura que:
     *   - En cada iteración, TODOS los threads alcanzan el punto de sincronización
     *   - Ningún thread avanza hasta que el último thread llegue
     *   - Permite que el thread 0 calcule el residual global sin race conditions
     */
    if (pthread_barrier_init(&barrier, NULL, (unsigned)ctx->workers) != 0) {
        free(threads);
        free(tasks);
        return -1;
    }

    /*
     * PASO 3: CONFIGURAR ESTRUCTURAS ThreadTask PARA CADA WORKER
     * 
     * Cada worker recibe un ThreadTask que contiene:
     *   - Su ID único (tid)
     *   - Su rango de nodos a procesar [start, end)
     *   - Punteros a estructuras compartidas (barrier, residuales, etc)
     * 
     * IMPORTANTE: Los workers NO copian estos punteros; todos comparten
     * la MISMA memoria, lo que permite comunicación entre threads.
     */
    for (t = 0; t < ctx->workers; ++t) {
        tasks[t].ctx = ctx;
        tasks[t].tid = t;
        tasks[t].workers = ctx->workers;
        tasks[t].barrier = &barrier;
        tasks[t].local_residual_sq_sums = local_residual_sq_sums;
        tasks[t].stop_flag = &stop_flag;
        tasks[t].sweeps_done = &sweeps_done;
        tasks[t].global_residual_rms = &global_residual_rms;
        
        /* Asigna el rango de nodos que este thread procesará */
        split_range(ctx->n, ctx->workers, t, &tasks[t].start, &tasks[t].end);

        /*
         * CREACIÓN DE THREAD
         * 
         * pthread_create(thread_id, atributos, función_entry, argumento)
         *   - thread_id: puntero a pthread_t donde se guarda el ID del nuevo thread
         *   - atributos: NULL (usar defaults: joinable, sin nombre, etc)
         *   - función_entry: thread_worker (función que ejecutará)
         *   - argumento: &tasks[t] (puntero a la estructura de parámetros)
         * 
         * La creación es ASINCRÓNICA: pthread_create retorna inmediatamente,
         * y el nuevo thread comienza su ejecución en paralelo.
         * 
         * MANEJO DE ERRORES: Si falla crear el thread t, unirse a los threads
         * ya creados (0 a t-1), destruir la barrera, y retornar error.
         */
        if (pthread_create(&threads[t], NULL, thread_worker, &tasks[t]) != 0) {
            /* Fallo: unirse a threads previamente creados */
            for (i = 0; i < t; ++i) {
                pthread_join(threads[i], NULL);
            }
            pthread_barrier_destroy(&barrier);
            free(threads);
            free(tasks);
            free(local_residual_sq_sums);
            return -1;
        }
    }

    /*
     * PASO 4: ESPERAR A QUE TODOS LOS WORKERS TERMINEN
     * 
     * pthread_join(thread_id, retval_ptr)
     *   - thread_id: thread a esperar
     *   - retval_ptr: NULL (no necesitamos el valor de retorno)
     * 
     * Esto BLOQUEA el thread main hasta que el worker indicado haya terminado.
     * Solo después de que TODOS los joins completen, el main continúa.
     * 
     * Nota: Los workers terminan por:
     *   a) Convergencia (break en el loop de iteraciones)
     *   b) Máximo de iteraciones alcanzado (ctx->nsweeps)
     */
    for (t = 0; t < ctx->workers; ++t) {
        pthread_join(threads[t], NULL);
    }

    /*
     * PASO 5: LIMPIAR RECURSOS GLOBALES
     * 
     * Destruye la barrera (libera su estado interno) y libera memoria dinámica.
     */
    pthread_barrier_destroy(&barrier);
    free(threads);
    free(tasks);
    free(local_residual_sq_sums);

    /*
     * PASO 6: GARANTIZAR COHERENCIA DE DATOS (Copiar si es necesario)
     * 
     * Debido a la estrategia par/impar de buffers, la solución final podría
     * estar en utmp en lugar de u si el número de iteraciones fue impar.
     * 
     * Este paso asegura que ctx->u contenga la solución final:
     *   - Si sweeps_done es IMPAR: u tiene datos antiguos, copiar desde utmp
     *   - Si sweeps_done es PAR: u tiene los datos correctos
     * 
     * Condición: (sweeps_done & 1) != 0 significa sweeps_done es impar
     */
    if ((sweeps_done & 1) != 0) {
        for (i = 1; i < ctx->n - 1; ++i) {
            ctx->u[i] = ctx->utmp[i];
        }
    }

    /*
     * PASO 7: ACTUALIZAR CONTEXTO CON RESULTADOS FINALES
     * 
     * Almacena:
     *   - sweeps_done: número real de iteraciones ejecutadas
     *   - last_error: residual RMS final alcanzado
     */
    ctx->sweeps_done = sweeps_done;
    ctx->last_error = global_residual_rms;

    return 0;
}
