#include "jacobi.h"

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

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

static void split_range(int n, int workers, int tid, int *start, int *end) {
    int interior = n - 1;
    int base = interior / workers;
    int extra = interior % workers;
    int count = base + (tid < extra ? 1 : 0);
    int offset = tid * base + (tid < extra ? tid : extra);

    *start = 1 + offset;
    *end = *start + count;
}

static void *thread_worker(void *arg) {
    ThreadTask *task = (ThreadTask *)arg;
    JacobiContext *ctx = task->ctx;
    int sweep;
    int i;

    for (sweep = 0; sweep < ctx->nsweeps; ++sweep) {
        double local_residual_sq_sum = 0.0;

        if ((sweep & 1) == 0) {
            for (i = task->start; i < task->end; ++i) {
                ctx->utmp[i] = 0.5 * (ctx->u[i - 1] + ctx->u[i + 1] + ctx->h2 * ctx->f[i]);
            }

            for (i = task->start; i < task->end; ++i) {
                double ri = (-ctx->utmp[i - 1] + 2.0 * ctx->utmp[i] - ctx->utmp[i + 1]) / ctx->h2 - ctx->f[i];
                local_residual_sq_sum += ri * ri;
            }
        } else {
            for (i = task->start; i < task->end; ++i) {
                ctx->u[i] = 0.5 * (ctx->utmp[i - 1] + ctx->utmp[i + 1] + ctx->h2 * ctx->f[i]);
            }

            for (i = task->start; i < task->end; ++i) {
                double ri = (-ctx->u[i - 1] + 2.0 * ctx->u[i] - ctx->u[i + 1]) / ctx->h2 - ctx->f[i];
                local_residual_sq_sum += ri * ri;
            }
        }

        task->local_residual_sq_sums[task->tid] = local_residual_sq_sum;
        pthread_barrier_wait(task->barrier);

        if (task->tid == 0) {
            double global_sq_sum = 0.0;
            int t;

            for (t = 0; t < task->workers; ++t) {
                global_sq_sum += task->local_residual_sq_sums[t];
            }

            *task->global_residual_rms = sqrt(global_sq_sum / (double)(ctx->n + 1));
            *task->sweeps_done = sweep + 1;
            *task->stop_flag = (*task->global_residual_rms <= ctx->tolerance) ? 1 : 0;
        }

        pthread_barrier_wait(task->barrier);
        if (*task->stop_flag != 0) {
            break;
        }
    }

    return NULL;
}

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

    threads = (pthread_t *)malloc((size_t)ctx->workers * sizeof(pthread_t));
    tasks = (ThreadTask *)malloc((size_t)ctx->workers * sizeof(ThreadTask));
    local_residual_sq_sums = (double *)malloc((size_t)ctx->workers * sizeof(double));
    if (threads == NULL || tasks == NULL || local_residual_sq_sums == NULL) {
        free(threads);
        free(tasks);
        free(local_residual_sq_sums);
        return -1;
    }

    if (pthread_barrier_init(&barrier, NULL, (unsigned)ctx->workers) != 0) {
        free(threads);
        free(tasks);
        return -1;
    }

    for (t = 0; t < ctx->workers; ++t) {
        tasks[t].ctx = ctx;
        tasks[t].tid = t;
        tasks[t].workers = ctx->workers;
        tasks[t].barrier = &barrier;
        tasks[t].local_residual_sq_sums = local_residual_sq_sums;
        tasks[t].stop_flag = &stop_flag;
        tasks[t].sweeps_done = &sweeps_done;
        tasks[t].global_residual_rms = &global_residual_rms;
        split_range(ctx->n, ctx->workers, t, &tasks[t].start, &tasks[t].end);

        if (pthread_create(&threads[t], NULL, thread_worker, &tasks[t]) != 0) {
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

    for (t = 0; t < ctx->workers; ++t) {
        pthread_join(threads[t], NULL);
    }

    pthread_barrier_destroy(&barrier);
    free(threads);
    free(tasks);
    free(local_residual_sq_sums);

    if ((sweeps_done & 1) != 0) {
        for (i = 1; i < ctx->n; ++i) {
            ctx->u[i] = ctx->utmp[i];
        }
    }

    ctx->sweeps_done = sweeps_done;
    ctx->last_error = global_residual_rms;

    return 0;
}
