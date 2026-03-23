CC      = gcc
CSTD    = -std=c11 -D_XOPEN_SOURCE=700 -Iinclude
CWARN   = -Wall -Wextra
CFLAGS_NOOPT = $(CWARN) $(CSTD) -O0
CFLAGS_O3    = $(CWARN) $(CSTD) -O3
LDFLAGS =
THREADS = -pthread

all: JacobiSec JacobiSecMem JacobiSecO3 JacobiHilos JacobiProc

JacobiSec:
	$(CC) $(CFLAGS_NOOPT) -o $@ src/common.c src/jacobi_seq.c src/main_seq.c $(LDFLAGS)

JacobiSecMem:
	$(CC) $(CFLAGS_NOOPT) -o $@ src/common.c src/jacobi_seq_mem.c src/main_seq_mem.c $(LDFLAGS)

JacobiSecO3:
	$(CC) $(CFLAGS_O3) -o $@ src/common.c src/jacobi_seq.c src/main_seq.c $(LDFLAGS)

JacobiHilos:
	$(CC) $(CFLAGS_NOOPT) -o $@ src/common.c src/jacobi_threads.c src/main_threads.c $(THREADS) $(LDFLAGS)

JacobiProc:
	$(CC) $(CFLAGS_NOOPT) -o $@ src/common.c src/jacobi_processes.c src/main_processes.c $(LDFLAGS)

clean:
	rm -f src/*.o JacobiSec JacobiSecMem JacobiSecO3 JacobiHilos JacobiProc

.PHONY: all clean
