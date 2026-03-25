#include "jacobi.h"

#include <math.h>

/*
 * ============================================================
 *  JACOBI SECUENCIAL (BASE)
 * ============================================================
 *  Esta funcion resuelve el problema 1D de Poisson discretizado:
 *
 *      -u''(x) = f(x),   con u(0)=0 y u(1)=0
 *
 *  usando el metodo iterativo de Jacobi.
 *
 *  IDEA DEL METODO:
 *  - En cada barrido (sweep), recalculamos cada nodo interior i
 *    como promedio de vecinos + termino forzante:
 *
 *      u_new[i] = 0.5 * (u_old[i-1] + u_old[i+1] + h^2 * f[i])
 *
 *  - Repetimos hasta converger (residual RMS <= tolerance)
 *    o alcanzar el maximo de iteraciones (nsweeps).
 *
 *  ¿POR QUE ESTA IMPLEMENTACION ES ASI?
 *  - Usa doble buffer (u y utmp) para mantener Jacobi "puro":
 *    todos los nodos del barrido k se calculan desde valores
 *    del barrido k-1, sin contaminar resultados intermedios.
 *  - Alterna buffers por paridad de sweep para evitar una copia
 *    completa en cada iteracion (ahorra ancho de banda de memoria).
 */
void jacobi_seq(JacobiContext *ctx) {
    /* Contadores de barrido e indice espacial de malla. */
    int sweep;
    int i;

    /* Metadatos finales de ejecucion (se guardan en ctx al terminar). */
    int sweeps_done = 0;
    double last_residual_rms = 0.0;

    /*
     * Bucle principal de Jacobi: cada vuelta = 1 barrido global.
     *
     * Recorremos a lo sumo nsweeps veces. Si convergemos antes,
     * hacemos break.
     */
    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        /*
         * active apuntara al arreglo que contiene la aproximacion
         * recien calculada en ESTE barrido (u o utmp segun paridad).
         */
        const double *active;

        /* Acumulador de ||residual||_2^2 para calcular RMS al final. */
        double residual_sq_sum = 0.0;

        /*
         * PASO 1: ACTUALIZACION JACOBI EN NODOS INTERIORES
         *
         * Barrido par  (0,2,4,...): lee de u     y escribe en utmp.
         * Barrido impar(1,3,5,...): lee de utmp  y escribe en u.
         *
         * Los bordes i=0 e i=n-1 no se tocan aqui (Dirichlet fijas).
         *
         * Ejemplo rapido (sin termino forzante, h^2 f[i] = 0):
         *   Estado inicial en u: [0, 1, 2, 3, 0]
         *
         *   sweep par (0): u -> utmp
         *     utmp[1]=(u[0]+u[2])/2=(0+2)/2=1
         *     utmp[2]=(u[1]+u[3])/2=(1+3)/2=2
         *     utmp[3]=(u[2]+u[n-1])/2=(2+0)/2=1
         *     utmp = [0, 1, 2, 1, 0]
         *
         *   sweep impar (1): utmp -> u
         *     u[1]=(utmp[0]+utmp[2])/2=(0+2)/2=1
         *     u[2]=(utmp[1]+utmp[3])/2=(1+1)/2=1
         *     u[3]=(utmp[2]+utmp[n-1])/2=(2+0)/2=1
         *     u = [0, 1, 1, 1, 0]
         *
         * Esto evita mezclar valores nuevos con viejos dentro del
         * mismo barrido y mantiene Jacobi matematicamente correcto.
         */
        if ((sweep & 1) == 0) {
            for (i = 1; i < ctx->n - 1; ++i) {
                ctx->utmp[i] = 0.5 * (ctx->u[i - 1] + ctx->u[i + 1] + ctx->h2 * ctx->f[i]);
            }
            active = ctx->utmp;
        } else {
            for (i = 1; i < ctx->n - 1; ++i) {
                ctx->u[i] = 0.5 * (ctx->utmp[i - 1] + ctx->utmp[i + 1] + ctx->h2 * ctx->f[i]);
            }
            active = ctx->u;
        }

        /*
         * PASO 2: MEDICION DE CONVERGENCIA (RESIDUAL RMS)
         *
         * En cada nodo interior i calculamos:
         *
         *   r_i = (-u[i-1] + 2u[i] - u[i+1]) / h^2 - f[i]
         *
         * Si r_i -> 0, la solucion satisface la ecuacion discreta.
         * Se acumula sum(r_i^2) para luego sacar RMS global.
         */
        for (i = 1; i < ctx->n - 1; ++i) {
            double ri = (-active[i - 1] + 2.0 * active[i] - active[i + 1]) / ctx->h2 - ctx->f[i];
            residual_sq_sum += ri * ri;
        }

        /*
         * PASO 3: ACTUALIZAR METRICAS Y VERIFICAR PARADA
         *
         * RMS = sqrt( sum(r_i^2) / n )
         * Se divide por n para normalizar por cantidad de nodos.
         */
        sweeps_done = sweep + 1;
        last_residual_rms = sqrt(residual_sq_sum / (double)ctx->n);

        /* Criterio de convergencia: residual bajo el umbral. */
        if (last_residual_rms <= ctx->tolerance) {
            break;
        }
    }

    /*
     * PASO 4: NORMALIZAR SALIDA
     *
     * Si terminamos con cantidad IMPAR de barridos, la ultima
     * solucion quedo en utmp. Copiamos a u para dejar el contrato
     * consistente: al salir, ctx->u siempre contiene la solucion.
     */
    if ((sweeps_done & 1) != 0) {
        for (i = 1; i < ctx->n - 1; ++i) {
            ctx->u[i] = ctx->utmp[i];
        }
    }

    /* PASO 5: Reportar resultados de convergencia al contexto. */
    ctx->sweeps_done = sweeps_done;
    ctx->last_error = last_residual_rms;
}
