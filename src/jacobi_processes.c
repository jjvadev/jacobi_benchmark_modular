/*
 * jacobi_processes.c: Implementación paralela del método de Jacobi usando procesos (fork).
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
 * ESTRATEGIA DE PARALELIZACIÓN CON PROCESOS
 * ============================================================================
 * 
 * Diferencia fundamental con jacobi_threads.c:
 *   
 *   THREADS:                     PROCESOS:
 *   - Comparten heap             - NO comparten memoria (fork copia)
 *   - Sincronización: barriers   - Sincronización: pipes + memoria mmap
 *   - Rápida pero vulnerable     - Aislada pero requiere IPC explícito
 *   
 * En jacobi_processes, el modelo es CLIENTE-SERVIDOR:
 * 
 *   SERVIDOR (Proceso padre - main)             CLIENTES (Procesos hijo)
 *   ├─ Crea memoria compartida (mmap)    ←→    ├─ Esperan en loop
 *   ├─ Hace fork() para cada hijo              ├─ Leen comando del padre
 *   ├─ LOOP de iteraciones:                    ├─ Actualizan su rango de nodos
 *   │  ├─ Envía comando 'A' o 'B' por pipe     ├─ Calculan residual local
 *   │  ├─ Espera ACK de todos los hijos  ←→    ├─ Escriben ACK al padre
 *   │  ├─ Reduce residuales globales            └─ Vuelven a esperar
 *   │  └─ Verifica convergencia                 
 *   ├─ Envía comando 'Q' para terminar
 *   ├─ Espera con waitpid() a todos
 *   └─ Copia resultado final y limpia
 * 
 * ============================================================================
 * MEMORIA COMPARTIDA VÍA MMAP
 * ============================================================================
 * 
 * Cuando fork() crea un proceso hijo, el hijo obtiene una copia del address
 * space del padre (copy-on-write en sistemas modernos), lo que significa que
 * cambios en variables heap son LOCALES al proceso.
 * 
 * Para comunicar datos entre procesos padre e hijos, se requiere memoria
 * EXPLÍCITAMENTE COMPARTIDA:
 * 
 *   mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0)
 *   
 * Esto aloca 'bytes' de memoria que:
 *   - Es COMPARTIDA: cambios visibles en todos los procesos
 *   - Es ANÓNIMA: no respaldada por archivo, solo en RAM
 *   - Es AISLADA: no interfiere con el heap del proceso
 * 
 * En jacobi_processes se usan cuatro arrays compartidos:
 *   - sa.u[]      : buffer para iteraciones pares (lee/escribe)
 *   - sa.utmp[]   : buffer para iteraciones impares (lee/escribe)
 *   - sa.f[]      : término fuente (solo lectura)
 *   - sa.local_residual_sq_sums[] : suma local de cada proceso
 * 
 * ============================================================================
 * PIPES PARA SINCRONIZACIÓN
 * ============================================================================
 * 
 * Un pipe es una comunicación unidireccional entre procesos:
 *   - Un extremo ESCRIBE, otro EXTREMO LEEEE
 *   - Operación BLOQUEANTE: si no hay datos, el lector espera
 * 
 * Se usan DOS pipes por proceso hijo:
 *   - cmd_parent_to_child[2*w] : padre ESCRIBE comando, hijo LEE
 *   - ack_child_to_parent[2*w] : hijo ESCRIBE ACK, padre LEE
 * 
 * Protocolo de sincronización por iteración:
 *   
 *   PADRE                          HIJO
 *   ────────────────────────────────────────
 *   ├─ write('A' o 'B')  ──→      BLOQUEADO (esperando comando)
 *                                  ├─ lee comando
 *                                  ├─ actualiza su rango
 *                                  ├─ calcula residual local
 *                      ←────  write('K')
 *   ├─ read() para todos hijos
 *   ├─ reduce residuales
 *   ├─ verifica convergencia
 *   
 * Utilidad de pipes:
 *   ✓ Garantizan orden: el hijo DEBE actualizar antes del ACK
 *   ✓ Evita busy-waiting (spinning): el hijo duerme en read()
 *   ✓ Escalable: cada proceso tiene su comunicación privada
 */

#include "jacobi.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
 * SharedArrays: encapsula punteros a los arrays asignados con mmap(),
 * que son COMPARTIDOS entre todos los procesos.
 * 
 * Nota: estas son COPIAS de los punteros; cuando fork() ocurre, cada proceso
 * tiene su propios valores de estos punteros, PERO apuntan a LA MISMA memoria
 * isma física (gracias a MAP_SHARED).
 */
typedef struct {
    double *u;                        /* Array de solución (alternancia par) */
    double *utmp;                     /* Array de solución (alternancia impar) */
    double *f;                        /* Término fuente (constante) */
    double *local_residual_sq_sums;   /* Suma cuadrática local por proceso */
} SharedArrays;

/*
 * split_range: Divide el dominio interior [1, n-2] equitativamente.
 * (Idéntico a jacobi_threads.c, ver comentarios allá)
 */
static void split_range(int n, int workers, int wid, int *start, int *end) {
    int interior = n - 2;
    int base = interior / workers;
    int extra = interior % workers;
    int count = base + (wid < extra ? 1 : 0);
    int offset = wid * base + (wid < extra ? wid : extra);

    *start = 1 + offset;
    *end = *start + count;
}

/*
 * map_shared_bytes: Aloca memoria COMPARTIDA entre procesos.
 * 
 * mmap() flags explicados:
 *   PROT_READ|PROT_WRITE    : permiso de lectura y escritura
 *   MAP_SHARED|MAP_ANONYMOUS: compartida entre procesos, no respaldada por archivo
 *   -1, 0                    : sin archivo (MAP_ANONYMOUS ignora fd y offset)
 * 
 * Retorna:
 *   Un puntero válido a 'bytes' de memoria compartida, o NULL si falla
 * 
 * Garantía:
 *   Cambios realizados en cualquier proceso son visibles en todos los demás
 */
static void *map_shared_bytes(size_t bytes) {
    void *ptr = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    return ptr;
}

/*
 * cleanup_shared: Libera la memoria compartida alojada con mmap().
 * 
 * Es crítico llamar munmap() para cada región de memoria mmap() alojada,
 * porque es recurso del sistema operativo, no simplemente heap.
 * 
 * Se llama tanto en el padre como en cada hijo para garantizar que
 * la memoria se libere correctamente cuando el último proceso termine.
 */
static void cleanup_shared(SharedArrays *sa, int n) {
    size_t bytes = (size_t)n * sizeof(double);
    if (sa->u != NULL) {
        munmap(sa->u, bytes);
    }
    if (sa->utmp != NULL) {
        munmap(sa->utmp, bytes);
    }
    if (sa->f != NULL) {
        munmap(sa->f, bytes);
    }
    if (sa->local_residual_sq_sums != NULL) {
        munmap(sa->local_residual_sq_sums, (size_t)MAX_WORKERS * sizeof(double));
    }
}

/*
 * jacobi_processes: Función principal que coordina la ejecución paralela con procesos.
 * 
 * ============================================================================
 * FLUJO DE EJECUCIÓN GENERAL
 * ============================================================================
 * 
 * Este procedimiento crea múltiples procesos hijo que ejecutan el método
 * de Jacobi en paralelo, coordinados por el proceso padre (main).
 * 
 * Esquema temporal:
 * 
 *   Proceso PADRE (main process, pid = original)
 *   ├─ Aloca memoria compartida (mmap) para u, utmp, f
 *   ├─ Copia datos iniciales a memoria compartida
 *   ├─ Crea pipes de comunicación
 *   ├─ fork() para cada hijo
 *   └─ LOOP de iteraciones (orquestación):
 *      ├─ Envía comando 'A' o 'B' a cada hijo
 *      ├─ Lee ACK de cada hijo
 *      ├─ Reduce residuales globales
 *      └─ Verifica convergencia
 * 
 *   Procesos HIJO (creados por fork())
 *   ├─ Cierran fds no necesarios
 *   ├─ LOOP esperando comandos:
 *   │  ├─ Lee comando del padre (bloqueante)
 *   │  ├─ Si 'Q': termina (break)
 *   │  ├─ Si 'A': actualiza buffer utmp, calcula residual local
 *   │  ├─ Si 'B': actualiza buffer u, calcula residual local
 *   │  └─ Envía ACK al padre
 *   └─ _exit(0)
 */
int jacobi_processes(JacobiContext *ctx) {
    int *cmd_parent_to_child = NULL;
    int *ack_child_to_parent = NULL;
    pid_t *pids = NULL;
    SharedArrays sa;
    size_t bytes;
    int w;
    int i;
    int status;
    char command;
    int sweeps_done = 0;
    double last_residual_rms = 0.0;

    if (ctx == NULL) {
        return -1;
    }

    /*
     * PASO 1: ALOCACIÓN DE MEMORIA COMPARTIDA (MMAP)
     * 
     * Se crean 4 arrays compartidos entre todos los procesos:
     *   - u, utmp: solución (buffers que se alternan por paridad)
     *   - f: término fuente (constante)
     *   - local_residual_sq_sums: para que cada hijo almacene su suma local
     * 
     * Nota: estos punteros se COPIARÁN a los procesos hijo por fork(),
     * pero seguirán apuntando a LA MISMA memoria física (gracias a MAP_SHARED).
     */
    memset(&sa, 0, sizeof(sa));
    bytes = (size_t)ctx->n * sizeof(double);
    sa.u = (double *)map_shared_bytes(bytes);
    sa.utmp = (double *)map_shared_bytes(bytes);
    sa.f = (double *)map_shared_bytes(bytes);
    sa.local_residual_sq_sums = (double *)map_shared_bytes((size_t)MAX_WORKERS * sizeof(double));
    if (sa.u == NULL || sa.utmp == NULL || sa.f == NULL || sa.local_residual_sq_sums == NULL) {
        cleanup_shared(&sa, ctx->n);
        return -1;
    }

    /*
     * PASO 2: COPIAR DATOS INICIALES A MEMORIA COMPARTIDA
     * 
     * Los datos se originan en ctx->u, ctx->utmp, ctx->f (memoria privada del padre).
     * Se copian a memoria mmap compartida para que los hijos los vean.
     * 
     * CRÍTICO: Los hijos NO ven cambios en ctx->u después de fork(),
     * solo ven cambios en sa.u (la memoria compartida mmap).
     */
    memcpy(sa.u, ctx->u, bytes);
    memcpy(sa.utmp, ctx->utmp, bytes);
    memcpy(sa.f, ctx->f, bytes);
    for (i = 0; i < MAX_WORKERS; ++i) {
        sa.local_residual_sq_sums[i] = 0.0;
    }

    /*
     * PASO 3: ALOCACIÓN DE PIPES PARA COMUNICACIÓN
     * 
     * Se crean 2 pipes por cada proceso hijo:
     *   - cmd_parent_to_child[2*w, 2*w+1]: padre escribe, hijo lee
     *   - ack_child_to_parent[2*w, 2*w+1]: hijo escribe, padre lee
     * 
     * Estructura de un pipe: int fd[2]
     *   - fd[0]: extremo de lectura
     *   - fd[1]: extremo de escritura
     */
    cmd_parent_to_child = (int *)malloc((size_t)ctx->workers * 2 * sizeof(int));
    ack_child_to_parent = (int *)malloc((size_t)ctx->workers * 2 * sizeof(int));
    pids = (pid_t *)malloc((size_t)ctx->workers * sizeof(pid_t));
    if (cmd_parent_to_child == NULL || ack_child_to_parent == NULL || pids == NULL) {
        free(cmd_parent_to_child);
        free(ack_child_to_parent);
        free(pids);
        cleanup_shared(&sa, ctx->n);
        return -1;
    }

    /*
     * PASO 4: CREAR PROCESOS HIJO CON FORK()
     * 
     * Para cada worker w, se:
     *   1. Create dos pipes (cmd y ack)
     *   2. fork() para crear un proceso hijo
     *   3. En el hijo (pids[w] == 0): entra en loop esperando comandos
     *   4. En el padre (pids[w] > 0): continúa creando más hijos
     */
    for (w = 0; w < ctx->workers; ++w) {
        int start;
        int end;
        int *cmd = &cmd_parent_to_child[2 * w];
        int *ack = &ack_child_to_parent[2 * w];

        /*
         * Crear dos pipes:
         *   cmd[0]: lectura, cmd[1]: escritura
         *   ack[0]: lectura, ack[1]: escritura
         */
        if (pipe(cmd) != 0 || pipe(ack) != 0) {
            free(cmd_parent_to_child);
            free(ack_child_to_parent);
            free(pids);
            cleanup_shared(&sa, ctx->n);
            return -1;
        }

        /*
         * Asignar rango de nodos que este worker procesará
         */
        split_range(ctx->n, ctx->workers, w, &start, &end);

        /*
         * FORK: Aquí ocurre la división en dos procesos
         */
        pids[w] = fork();

        if (pids[w] < 0) {
            /* Error en fork() */
            free(cmd_parent_to_child);
            free(ack_child_to_parent);
            free(pids);
            cleanup_shared(&sa, ctx->n);
            return -1;
        }

        /*
         * ========== CÓDIGO DEL PROCESO HIJO (pids[w] == 0) ==========
         */
        if (pids[w] == 0) {
            int j;
            char ack_byte = 'K';

            /*
             * CERRAR EXTREMOS NO NECESARIOS DE LOS PIPES
             * 
             * El hijo solo necesita:
             *   - Leer del pipe cmd: cierra cmd[1] (escritura)
             *   - Escribir al pipe ack: cierra ack[0] (lectura)
             */
            close(cmd[1]);
            close(ack[0]);

            /*
             * LOOP DE PROCESAMIENTO SÍNCRONO
             * 
             * El hijo espera comandos del padre. Cada comando especifica:
             *   - 'A': iteración par (lee u, escribe utmp)
             *   - 'B': iteración impar (lee utmp, escribe u)
             *   - 'Q': quit (terminar)
             * 
             * El read() es BLOQUEANTE: si el padre no ha escrito nada,
             * el hijo duerme aquí sin consumir CPU (eficiente).
             */
            while (read(cmd[0], &command, 1) == 1) {
                if (command == 'Q') {
                    /*
                     * Comando de parada: salir del loop
                     */
                    break;
                } else if (command == 'A') {
                    /*
                     * FASE PAR: Lee desde sa.u, escribe en sa.utmp
                     * 
                     * Cada hijo actualiza SOLO su rango [start, end).
                     * Los demás hijos actualizan sus rangos en paralelo.
                     */
                    double local_sum = 0.0;
                    for (j = start; j < end; ++j) {
                        sa.utmp[j] = 0.5 * (sa.u[j - 1] + sa.u[j + 1] + ctx->h2 * sa.f[j]);
                        {
                            /*
                             * Cálculo del residual en este nodo
                             * r_j = (-utmp[j-1] + 2*utmp[j] - utmp[j+1])/h² - f[j]
                             */
                            double ri = (-sa.utmp[j - 1] + 2.0 * sa.utmp[j] - sa.utmp[j + 1]) / ctx->h2 - sa.f[j];
                            local_sum += ri * ri;
                        }
                    }
                    /* Almacena suma local en memoria compartida */
                    sa.local_residual_sq_sums[w] = local_sum;
                } else if (command == 'B') {
                    /*
                     * FASE IMPAR: Lee desde sa.utmp, escribe en sa.u
                     */
                    double local_sum = 0.0;
                    for (j = start; j < end; ++j) {
                        sa.u[j] = 0.5 * (sa.utmp[j - 1] + sa.utmp[j + 1] + ctx->h2 * sa.f[j]);
                        {
                            double ri = (-sa.u[j - 1] + 2.0 * sa.u[j] - sa.u[j + 1]) / ctx->h2 - sa.f[j];
                            local_sum += ri * ri;
                        }
                    }
                    sa.local_residual_sq_sums[w] = local_sum;
                }

                /*
                 * ENVIAR ACK AL PADRE
                 * 
                 * Solo después de completar la actualización y el cálculo del residual,
                 * el hijo escribe un byte de ACK al padre. Esto garantiza que:
                 *   - El padre no procede al siguiente paso hasta que el hijo termina
                 *   - No hay race conditions en sa.u o sa.utmp
                 */
                if (command == 'A' || command == 'B') {
                    if (write(ack[1], &ack_byte, 1) != 1) {
                        break;
                    }
                }
            }

            /*
             * FIN DE EJECUCIÓN DEL HIJO
             * 
             * Cerrar los pipes y liberar memoria compartida
             * _exit(0) termina el proceso sin ejecutar atexit handlers
             */
            close(cmd[0]);
            close(ack[1]);
            cleanup_shared(&sa, ctx->n);
            _exit(0);
        }
        /* ========== FIN DEL CÓDIGO DEL HIJO ========== */

        /*
         * ========== CÓDIGO DEL PADRE (pids[w] > 0) ==========
         * 
         * El padre cierra los extremos de los pipes que el hijo usa:
         *   - cmd[0]: no leeremos (el hijo lee)
         *   - ack[1]: no escribiremos (el hijo escribe)
         * 
         * Mantiene abiertos:
         *   - cmd[1]: para escribir comandos
         *   - ack[0]: para leer ACKs
         */
        close(cmd[0]);
        close(ack[1]);
    }
    /* ========== FIN DE CREACIÓN DE HIJOS ========== */

    /*
     * PASO 5: LOOP PRINCIPAL DE ITERACIONES (EN EL PADRE)
     * 
     * El padre ahora orquesta ctx->nsweeps iteraciones.
     * En cada iteración:
     *   1. Envía comando de fase ('A' o 'B')
     *   2. Espera ACK de cada hijo (bloqueante)
     *   3. Reduce residuales locales a una suma global
     *   4. Verifica convergencia
     */
    for (i = 0; i < ctx->nsweeps; ++i) {
        char phase = ((i & 1) == 0) ? 'A' : 'B';
        char ack_byte;

        /*
         * ENVIAR COMANDO A TODOS LOS HIJOS
         * 
         * El padre escribe el comando 'A' o 'B' en el pipe de cada hijo.
         * Esto despierta al hijo del bloqueo en read() y lo instruye
         * qué actualización realizar.
         */
        for (w = 0; w < ctx->workers; ++w) {
            if (write(cmd_parent_to_child[2 * w + 1], &phase, 1) != 1) {
                return -1;
            }
        }

        /*
         * ESPERAR ACK DE TODOS LOS HIJOS
         * 
         * El padre lee un byte de cada hijo. Este es un punto de sincronización:
         * el padre BLOQUEA hasta que TODOS los hijos hayan completado su trabajo
         * y enviado el ACK.
         */
        for (w = 0; w < ctx->workers; ++w) {
            if (read(ack_child_to_parent[2 * w], &ack_byte, 1) != 1) {
                return -1;
            }
        }

        /*
         * REDUCCIÓN DE RESIDUAL GLOBAL (EN EL PADRE)
         * 
         * El padre suma las contribuciones locales de residuales de cada hijo.
         * Esto solo es posible DESPUÉS de que todos los hijos hayan escrito
         * sus valores en sa.local_residual_sq_sums[].
         * 
         * Cálculo del RMS global:
         *   rms = sqrt(sum de todos los r_i²) / n
         */
        {
            double global_sq_sum = 0.0;
            for (w = 0; w < ctx->workers; ++w) {
                global_sq_sum += sa.local_residual_sq_sums[w];
            }
            sweeps_done = i + 1;
            last_residual_rms = sqrt(global_sq_sum / (double)ctx->n);
            
            /*
             * VERIFICACIÓN DE CONVERGENCIA
             * 
             * Si el residual RMS es ≤ tolerancia, se alcanzó convergencia.
             * El padre sale del loop de iteraciones.
             */
            if (last_residual_rms <= ctx->tolerance) {
                break;
            }
        }
    }
    /* ========== FIN DEL LOOP DE ITERACIONES ========== */

    /*
     * PASO 6: TERMINACIÓN DE PROCESOS HIJO
     * 
     * El padre envía comando 'Q' (quit) a todos los hijos para instruirles
     * que terminen. Sin esto, los hijos seguirían esperando comandos.
     * 
     * Luego cierra los extremos de los pipes que está usando.
     */
    command = 'Q';
    for (w = 0; w < ctx->workers; ++w) {
        write(cmd_parent_to_child[2 * w + 1], &command, 1);
        close(cmd_parent_to_child[2 * w + 1]);  /* Cierra extremo de escritura */
        close(ack_child_to_parent[2 * w]);      /* Cierra extremo de lectura */
    }

    /*
     * ESPERAR A QUE TODOS LOS HIJOS TERMINEN
     * 
     * waitpid(pid, status_ptr, flags) bloquea al padre hasta que el proceso
     * 'pid' termine. El padre lo hace para cada hijo secuencialmente.
     * 
     * Esto asegura que todos los hijos hayan limpiado sus recursos antes
     * de que el padre continúe.
     */
    for (w = 0; w < ctx->workers; ++w) {
        waitpid(pids[w], &status, 0);
    }

    /*
     * PASO 7: RECUPERAR RESULTADO FINAL
     * 
     * Debido a la alternancia par/impar, el resultado final podría estar
     * en u o utmp dependiendo del número de iteraciones.
     * 
     *   Si sweeps_done es PAR (sweeps_done & 1 == 0):
     *      → último buffer escritura fue en utmp durante iteración impar anterior
     *      → u contiene el resultado correcto
     *   Si sweeps_done es IMPAR (sweeps_done & 1 == 1):
     *      → última iteración fue par, escribió en utmp
     *      → utmp contiene el resultado, copiar a u
     */
    if ((sweeps_done & 1) == 0) {
        /* Resultado en sa.u, copiar a ctx->u */
        memcpy(ctx->u, sa.u, bytes);
    } else {
        /* Resultado en sa.utmp, copiar a ctx->u */
        memcpy(ctx->u, sa.utmp, bytes);
    }

    /*
     * PASO 8: ACTUALIZAR CONTEXTO Y LIMPIAR
     * 
     * Almacena metadatos de la ejecución y libera toda la memoria compartida.
     */
    ctx->sweeps_done = sweeps_done;
    ctx->last_error = last_residual_rms;

    free(cmd_parent_to_child);
    free(ack_child_to_parent);
    free(pids);
    cleanup_shared(&sa, ctx->n);
    return 0;
}
