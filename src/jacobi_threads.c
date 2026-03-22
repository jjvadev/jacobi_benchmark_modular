#include "jacobi.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    JacobiContext *ctx;
    int tid;
    int start;
    int end;
    pthread_barrier_t *barrier;
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
        if ((sweep & 1) == 0) {
            for (i = task->start; i < task->end; ++i) {
                ctx->utmp[i] = 0.5 * (ctx->u[i - 1] + ctx->u[i + 1] + ctx->h2 * ctx->f[i]);
            }
        } else {
            for (i = task->start; i < task->end; ++i) {
                ctx->u[i] = 0.5 * (ctx->utmp[i - 1] + ctx->utmp[i + 1] + ctx->h2 * ctx->f[i]);
            }
        }
        pthread_barrier_wait(task->barrier);
    }

    return NULL;
}

int jacobi_threads(JacobiContext *ctx) {
    pthread_t *threads;
    ThreadTask *tasks;
    pthread_barrier_t barrier;
    int t;
    int i;

    if (ctx == NULL) {
        return -1;
    }

    threads = (pthread_t *)malloc((size_t)ctx->workers * sizeof(pthread_t));
    tasks = (ThreadTask *)malloc((size_t)ctx->workers * sizeof(ThreadTask));
    if (threads == NULL || tasks == NULL) {
        free(threads);
        free(tasks);
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
        tasks[t].barrier = &barrier;
        split_range(ctx->n, ctx->workers, t, &tasks[t].start, &tasks[t].end);

        if (pthread_create(&threads[t], NULL, thread_worker, &tasks[t]) != 0) {
            for (i = 0; i < t; ++i) {
                pthread_join(threads[i], NULL);
            }
            pthread_barrier_destroy(&barrier);
            free(threads);
            free(tasks);
            return -1;
        }
    }

    for (t = 0; t < ctx->workers; ++t) {
        pthread_join(threads[t], NULL);
    }

    pthread_barrier_destroy(&barrier);
    free(threads);
    free(tasks);

    if ((ctx->nsweeps & 1) != 0) {
        for (i = 1; i < ctx->n; ++i) {
            ctx->u[i] = ctx->utmp[i];
        }
    }

    return 0;
}
