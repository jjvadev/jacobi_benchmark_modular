/*
 * ============================================================
 *  JACOBI ITERATIVO — ECUACION DE POISSON 1D
 *  Version serial base — con copia explicita u_new -> u_old
 * ============================================================
 *
 *  DECISION DE DISENO:
 *  Usamos copia explicita (no swap de punteros) para que
 *  esta version sea estructuralmente identica a las versiones
 *  paralelas (hilos y procesos), donde el swap no es aplicable
 *  porque los punteros son compartidos entre trabajadores.
 *
 *  Esto garantiza que el speedup mida unicamente el efecto
 *  del paralelismo, no diferencias de algoritmo interno.
 *
 *  PROBLEMA QUE RESOLVEMOS:
 *
 *      -u''(x) = f(x)   en el intervalo [0, 1]
 *       u(0)   = 0
 *       u(1)   = 0
 *
 *  Donde:
 *      f(x)        = -x*(x+3)*exp(x)     <- fuerza conocida
 *      u_exacta(x) = x*(x-1)*exp(x)      <- solucion analitica
 *                                            (solo para verificar)
 *
 *  IDEA GENERAL:
 *  No podemos resolver la ecuacion diferencial directamente
 *  en una computadora. En cambio:
 *    1. Dividimos [0,1] en N puntos (la "malla")
 *    2. Convertimos la ecuacion diferencial en un sistema
 *       de ecuaciones algebraicas A*u = f
 *    3. Resolvemos ese sistema iterativamente con Jacobi
 *
 *  Compilar:
 *      gcc -O0 -o jacobi_serial jacobi_serial.c -lm
 *      (-O0 = sin optimizaciones del compilador, para medir
 *       el tiempo base real y luego comparar con -O2, -O3)
 *
 *  Ejecutar:
 *      ./jacobi_serial <k>     donde N = 2^k + 1
 *
 *  Tamanios tipicos para estudio de speedup:
 *      k=5  -> N=33       k=8  -> N=257
 *      k=6  -> N=65       k=9  -> N=513
 *      k=7  -> N=129      k=10 -> N=1025
 * ============================================================ */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>


/* ============================================================
 *  PARAMETROS DE CONVERGENCIA
 *
 *  TOL      : tolerancia del residual. Cuando el error
 *             residual caiga por debajo de este valor,
 *             consideramos que la solucion es suficientemente
 *             buena y paramos.
 *
 *  MAX_ITER : limite de seguridad. Si despues de este numero
 *             de iteraciones no convergimos, algo fallo.
 *             Jacobi puede ser muy lento (ver tabla en el
 *             documento): k=10 necesita ~3 millones de iters.
 * ============================================================ */
#define TOL      1.0e-6
#define MAX_ITER 5000000


/* ============================================================
 *  static inline double fuerza(x)
 * ============================================================
 *  Calcula f(x): el termino forzante del lado derecho
 *  de la ecuacion de Poisson.
 *
 *  En el problema fisico de la cuerda: es la fuerza externa
 *  aplicada en cada punto x. La cuerda se dobla segun esta.
 *
 *  "static"      : esta funcion solo existe en este archivo.
 *  "inline"      : el compilador pega el cuerpo de la funcion
 *                  directamente donde se llama, eliminando el
 *                  costo de la llamada (push/pop de stack).
 *                  Util porque se llama N veces en la init
 *                  y N*iter veces seria si no precalculasemos.
 * ============================================================ */
static inline double fuerza(double x)
{
    return -x * (x + 3.0) * exp(x);
}


/* ============================================================
 *  static inline double solucion_exacta(x)
 * ============================================================
 *  Calcula u(x): la solucion analitica real del problema.
 *
 *  En problemas reales NO existiria esta funcion — si la
 *  tuvieramos, no necesitariamos el metodo numerico.
 *  Aqui la usamos SOLO para verificar que nuestra
 *  aproximacion numerica sea correcta.
 * ============================================================ */
static inline double solucion_exacta(double x)
{
    return x * (x - 1.0) * exp(x);
}


/* ============================================================
 *  double get_time()
 * ============================================================
 *  Devuelve el tiempo actual en segundos con precision
 *  de nanosegundos.
 *
 *  CLOCK_MONOTONIC: un reloj que nunca retrocede y no se
 *  ve afectado por cambios del reloj del sistema (NTP, etc).
 *  Ideal para medir duraciones (wall time).
 *
 *  Usamos la diferencia get_time_fin - get_time_inicio
 *  para medir exactamente cuanto tarda el ciclo de Jacobi.
 * ============================================================ */
double get_time()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    /* tv_sec  = parte entera en segundos                    */
    /* tv_nsec = parte fraccionaria en nanosegundos (10^-9)  */
    return ts.tv_sec + ts.tv_nsec * 1.0e-9;
}



/* ============================================================
 *  MAIN
 * ============================================================ */

int main(int argc, char *argv[])
{

    /* ----------------------------------------------------------
     *  PASO 0: Leer el parametro k desde la linea de comandos
     *
     *  k define el tamanio de la malla: N = 2^k + 1
     *  Esta formula viene del metodo multigrid que requiere
     *  mallas anidables (potencias de 2 mas uno).
     *
     *  Ejemplos:
     *    k=3 -> N=9    (malla muy gruesa, converge en ~188 iter)
     *    k=5 -> N=33   (del documento: 3,088 iteraciones)
     *    k=10-> N=1025 (del documento: ~3.17 millones de iter)
     * ---------------------------------------------------------- */
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <k>   (N = 2^k + 1)\n", argv[0]);
        return 1;
    }

    int    k  = atoi(argv[1]);
  
    /* ----------------------------------------------------------
     *  PASO 1: Definir la geometria de la malla
     *
     *  N  : numero total de nodos, incluidos los dos extremos.
     *  h  : distancia uniforme entre nodos consecutivos.
     *       Si N=5 y el intervalo es [0,1], entonces h=0.25
     *       y los nodos estan en x=0, 0.25, 0.5, 0.75, 1.0
     *  h2 : h al cuadrado. Aparece en la formula de Jacobi.
     *       Lo precalculamos aqui para no repetir h*h en el loop.
     *
     *  (1 << k) es lo mismo que 2^k pero mas rapido:
     *  desplaza el bit 1 exactamente k posiciones a la izquierda.
     *  Ej: 1 << 3 = 0b1000 = 8, entonces N = 8+1 = 9
     * ---------------------------------------------------------- */
    int    N  = (1 << k) + 1;
    double h  = 1.0 / (double)(N - 1);
    double h2 = h * h;

    printf("=============================================\n");
    printf("  Jacobi 1D Poisson — Serial (copia)\n");
    printf("=============================================\n");
    printf("  k = %d  ->  N = %d nodos\n", k, N);
    printf("  h = %.8f\n", h);
    printf("  Tolerancia residual = %.1e\n", TOL);
    printf("=============================================\n\n");


    /* ----------------------------------------------------------
     *  PASO 2: Reservar memoria para los cuatro arreglos
     *
     *  u_old[N] : solucion de la iteracion ANTERIOR.
     *             En la primera iteracion, todos son cero
     *             (estimacion inicial: "no se nada").
     *
     *  u_new[N] : solucion que estamos calculando AHORA.
     *             Al terminar cada iteracion, se convierte en
     *             el nuevo u_old para la siguiente.
     *
     *  f[N]     : valores de la funcion fuerza en cada nodo.
     *             f[i] = fuerza(x[i])
     *             Se calcula UNA SOLA VEZ antes del ciclo,
     *             porque f(x) no cambia con las iteraciones.
     *
     *  x[N]     : coordenadas fisicas de cada nodo.
     *             x[i] = i * h    (de 0.0 hasta 1.0)
     *
     *  calloc vs malloc:
     *    calloc(n, size) = malloc(n*size) + pone todo a cero.
     *    Para u_old y u_new usamos calloc porque la condicion
     *    inicial de Jacobi es "empezar con todo en cero".
     *    Para f y x usamos malloc porque los llenamos enseguida
     *    con valores no-cero — inicializarlos a cero seria
     *    trabajo desperdiciado.
     * ---------------------------------------------------------- */
    double *u     = (double *)calloc(N, sizeof(double));
    double *u_new = (double *)calloc(N, sizeof(double));
    double *f     = (double *)malloc(N * sizeof(double));
    double *x     = (double *)malloc(N * sizeof(double));

    if (!u || !u_new || !f || !x) {
        fprintf(stderr, "Error: malloc/calloc fallo\n");
        return 1;
    }


    /* ----------------------------------------------------------
     *  PASO 3: Inicializar coordenadas x[] y fuerza f[]
     *
     *  x[i] = posicion fisica del nodo i en el intervalo [0,1].
     *  f[i] = valor de la funcion forzante en ese punto.
     *
     *  Guardamos f[i] = fuerza(x[i]) sin multiplicar por h2.
     *  La multiplicacion h2*f[i] ocurre dentro del loop de
     *  Jacobi, pero como h2 es una constante que el procesador
     *  mantiene en un registro, no hay costo extra apreciable.
     * ---------------------------------------------------------- */
    for (int i = 0; i < N; i++) {
        x[i] = i * h;
        f[i] = fuerza(x[i]);
    }


    /* ----------------------------------------------------------
     *  PASO 4: Fijar condiciones de frontera (Dirichlet)
     *
     *  El problema dice u(0) = 0 y u(1) = 0.
     *  calloc ya puso todo a cero, asi que tecnicamente esto
     *  es redundante, pero lo escribimos explicitamente para
     *  que quede claro en el codigo cuales nodos son fijos.
     *
     *  Los nodos de frontera NUNCA cambian durante las
     *  iteraciones — son datos del problema, no incognitas.
     * ---------------------------------------------------------- */
    u[0]   = 0.0;   u_new[0]   = 0.0;
    u[N-1] = 0.0;   u_new[N-1] = 0.0;

    /* --------------------------------------------------------
     *  CICLO ITERATIVO DE JACOBI
     * --------------------------------------------------------
     *  Estructura identica a las versiones paralelas:
     *
     *  Iteracion k:
     *    FASE A : calcular u_new[i] para todo i interior
     *             <- en paralelo: cada worker su rango
     *    FASE B : calcular residual RMS sobre u_new
     *             <- en paralelo: solo el worker 0
     *    FASE C : copiar u_new[i] -> u[i] para todo i
     *             <- en paralelo: cada worker su rango
     *
     *  En las versiones paralelas, entre cada fase hay una
     *  barrera de sincronizacion. En serial no hace falta
     *  porque todo ocurre secuencialmente en un solo hilo.
     * -------------------------------------------------------- */
    double t_inicio = get_time();

    int    iter    = 0;
    double rms_res = 1.0e10;

    for (iter = 0; iter < MAX_ITER; iter++)
    {
        /* ====================================================
         *  FASE A: actualizar todos los nodos interiores
         * ====================================================
        *  (A) ACTUALIZAR: calcular u_new desde u_old
         * ======================================================
         *  Esta es la formula central de Jacobi para Poisson 1D.
         *
         *  La ecuacion discreta en el nodo i es:
         *      (-u[i-1] + 2*u[i] - u[i+1]) / h2 = f[i]
         *
         *  Despejando u[i] (lo que queremos calcular):
         *      u_new[i] = 0.5 * (u_old[i-1] + u_old[i+1] + h2*f[i])
         *
         *  En palabras: el nuevo valor de cada nodo es el
         *  PROMEDIO de sus dos vecinos, mas una correccion
         *  proporcional a la fuerza aplicada en ese punto.
         *
         *  Observaciones importantes:
         *
         *  1. Solo actualizamos los nodos INTERIORES (i=1..N-2).
         *     Los extremos (i=0 e i=N-1) son condiciones de
         *     frontera fijas — no son incognitas.
         *
         *  Leemos u[] (valores actuales, solo lectura).
         *  Escribimos u_new[] (valores nuevos, escritura).
         *  Cada nodo i es independiente -> paralelizable.
         * ==================================================== */
        for (int i = 1; i < N - 1; i++) {
            u_new[i] = 0.5 * (u[i-1] + u[i+1] + h2 * f[i]);
        }

        /* ======================================================
         *  (B) RESIDUAL: medir que tan buena es la solucion
         * ======================================================
         *  El residual r[i] mide cuanto "viola" u_new la ecuacion
         *  que queremos resolver:
         *
         *      r[i] = A*u_new[i] - f[i]
         *           = (-u_new[i-1] + 2*u_new[i] - u_new[i+1])/h2
         *             - f[i]
         *
         *  Si u_new fuera la solucion perfecta, r[i] = 0.
         *  Un residual grande = estamos lejos de la solucion.
         *  Un residual pequeno = la solucion es buena.
         *
         *  Usamos la norma RMS (raiz de la media de los cuadrados)
         *  para tener una medida comparable entre distintos N:
         *
         *      rms_res = sqrt( sum(r[i]^2) / N )
         *
         *  Dividir por N hace que el valor no dependa de cuantos
         *  nodos tenga la malla. Sin eso, una malla mas fina
         *  (mas nodos) siempre tendria residual mas grande, y
         *  no podriamos comparar convergencia entre distintos k.
         * ====================================================== */
        double sum_res2 = 0.0;
        for (int i = 1; i < N - 1; i++) {
            double Au = (-u_new[i-1] + 2.0*u_new[i] - u_new[i+1]) / h2;
            double r  = Au - f[i];
            sum_res2 += r * r;
        }
        rms_res = sqrt(sum_res2 / (double)N);

        /* ====================================================
         *  FASE C: copiar u_new -> u
         * ====================================================
         *  En serial: un loop secuencial sobre todos los nodos.
         *
         *  Necesitamos que lo que era u_new se convierta en
         *  u_old para la proxima iteracion.
         * ==================================================== */
        for (int i = 0; i < N; i++) {
            u[i] = u_new[i];
        }

        /* Progreso cada 1000 iteraciones */
        if (iter % 1000 == 0 && iter > 0) {
            printf("  iter %6d  |  residual RMS = %.6e\n", iter, rms_res);
        }

        /* Criterio de parada: residual por debajo de la tolerancia */

        if (rms_res <= TOL) {
            iter++;
            break;
        }

    } /* fin ciclo Jacobi */

    double tiempo = get_time() - t_inicio;

    /* --------------------------------------------------------
     *  Resultados
     * -------------------------------------------------------- */
    printf("\n=============================================\n");
    printf("  RESULTADOS\n");
    printf("=============================================\n");
    printf("  Iteraciones realizadas : %d\n", iter);
    printf("  Residual RMS final     : %.6e\n", rms_res);
    printf("  Tiempo de computo      : %.6f segundos\n", tiempo);

    /* Calcular error contra la solucion exacta */
    double err2 = 0.0;
    for (int i = 0; i < N; i++) {
        double e = u[i] - solucion_exacta(x[i]);
        err2 += e * e;
    }
    printf("  Error RMS vs exacta    : %.6e\n", sqrt(err2 / (double)N));
    printf("=============================================\n");

      /* Tabla detallada solo para mallas pequenas (N <= 33) */
    if (N <= 33) {
        printf("\n  i      x         u_exacta     u_jacobi     error\n");
        printf("  ---------------------------------------------------\n");
        for (int i = 0; i < N; i++) {
            double ue = solucion_exacta(x[i]);
            printf("  %2d   %.4f    %10.6f   %10.6f   %9.2e\n",
                   i, x[i], ue, u[i], fabs(ue - u[i]));
        }
    }

    free(u);
    free(u_new);
    free(f);
    free(x);
    return 0;
}