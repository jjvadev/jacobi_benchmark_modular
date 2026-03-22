#include "jacobi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    double *u;
    double *utmp;
    double *f;
} SharedArrays;

static void split_range(int n, int workers, int wid, int *start, int *end) {
    int interior = n - 1;
    int base = interior / workers;
    int extra = interior % workers;
    int count = base + (wid < extra ? 1 : 0);
    int offset = wid * base + (wid < extra ? wid : extra);

    *start = 1 + offset;
    *end = *start + count;
}

static void *map_shared_bytes(size_t bytes) {
    void *ptr = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        return NULL;
    }
    return ptr;
}

static void cleanup_shared(SharedArrays *sa, int n) {
    size_t bytes = (size_t)(n + 1) * sizeof(double);
    if (sa->u != NULL) {
        munmap(sa->u, bytes);
    }
    if (sa->utmp != NULL) {
        munmap(sa->utmp, bytes);
    }
    if (sa->f != NULL) {
        munmap(sa->f, bytes);
    }
}

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

    if (ctx == NULL) {
        return -1;
    }

    memset(&sa, 0, sizeof(sa));
    bytes = (size_t)(ctx->n + 1) * sizeof(double);
    sa.u = (double *)map_shared_bytes(bytes);
    sa.utmp = (double *)map_shared_bytes(bytes);
    sa.f = (double *)map_shared_bytes(bytes);
    if (sa.u == NULL || sa.utmp == NULL || sa.f == NULL) {
        cleanup_shared(&sa, ctx->n);
        return -1;
    }

    memcpy(sa.u, ctx->u, bytes);
    memcpy(sa.utmp, ctx->utmp, bytes);
    memcpy(sa.f, ctx->f, bytes);

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

    for (w = 0; w < ctx->workers; ++w) {
        int start;
        int end;
        int *cmd = &cmd_parent_to_child[2 * w];
        int *ack = &ack_child_to_parent[2 * w];

        if (pipe(cmd) != 0 || pipe(ack) != 0) {
            free(cmd_parent_to_child);
            free(ack_child_to_parent);
            free(pids);
            cleanup_shared(&sa, ctx->n);
            return -1;
        }

        split_range(ctx->n, ctx->workers, w, &start, &end);
        pids[w] = fork();

        if (pids[w] < 0) {
            free(cmd_parent_to_child);
            free(ack_child_to_parent);
            free(pids);
            cleanup_shared(&sa, ctx->n);
            return -1;
        }

        if (pids[w] == 0) {
            int j;
            char ack_byte = 'K';
            close(cmd[1]);
            close(ack[0]);

            while (read(cmd[0], &command, 1) == 1) {
                if (command == 'Q') {
                    break;
                } else if (command == 'A') {
                    for (j = start; j < end; ++j) {
                        sa.utmp[j] = 0.5 * (sa.u[j - 1] + sa.u[j + 1] + ctx->h2 * sa.f[j]);
                    }
                } else if (command == 'B') {
                    for (j = start; j < end; ++j) {
                        sa.u[j] = 0.5 * (sa.utmp[j - 1] + sa.utmp[j + 1] + ctx->h2 * sa.f[j]);
                    }
                }
                if (command == 'A' || command == 'B') {
                    if (write(ack[1], &ack_byte, 1) != 1) {
                        break;
                    }
                }
            }

            close(cmd[0]);
            close(ack[1]);
            cleanup_shared(&sa, ctx->n);
            _exit(0);
        }

        close(cmd[0]);
        close(ack[1]);
    }

    for (i = 0; i < ctx->nsweeps; ++i) {
        char phase = ((i & 1) == 0) ? 'A' : 'B';
        char ack_byte;

        for (w = 0; w < ctx->workers; ++w) {
            if (write(cmd_parent_to_child[2 * w + 1], &phase, 1) != 1) {
                return -1;
            }
        }

        for (w = 0; w < ctx->workers; ++w) {
            if (read(ack_child_to_parent[2 * w], &ack_byte, 1) != 1) {
                return -1;
            }
        }
    }

    command = 'Q';
    for (w = 0; w < ctx->workers; ++w) {
        write(cmd_parent_to_child[2 * w + 1], &command, 1);
        close(cmd_parent_to_child[2 * w + 1]);
        close(ack_child_to_parent[2 * w]);
    }

    for (w = 0; w < ctx->workers; ++w) {
        waitpid(pids[w], &status, 0);
    }

    if ((ctx->nsweeps & 1) == 0) {
        memcpy(ctx->u, sa.u, bytes);
    } else {
        memcpy(ctx->u, sa.utmp, bytes);
    }

    free(cmd_parent_to_child);
    free(ack_child_to_parent);
    free(pids);
    cleanup_shared(&sa, ctx->n);
    return 0;
}
