CC      = gcc
CFLAGS  = -O3 -Wall -Wextra -std=c11 -D_XOPEN_SOURCE=700 -Iinclude
LDFLAGS =
THREADS = -pthread

COMMON_OBJS = src/common.o
SEQ_OBJS    = $(COMMON_OBJS) src/jacobi_seq.o src/main_seq.o
THR_OBJS    = $(COMMON_OBJS) src/jacobi_threads.o src/main_threads.o
PROC_OBJS   = $(COMMON_OBJS) src/jacobi_processes.o src/main_processes.o

all: JacobiSec JacobiHilos JacobiProc

JacobiSec: $(SEQ_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SEQ_OBJS) $(LDFLAGS)

JacobiHilos: $(THR_OBJS)
	$(CC) $(CFLAGS) -o $@ $(THR_OBJS) $(THREADS) $(LDFLAGS)

JacobiProc: $(PROC_OBJS)
	$(CC) $(CFLAGS) -o $@ $(PROC_OBJS) $(LDFLAGS)

src/common.o: src/common.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/jacobi_seq.o: src/jacobi_seq.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/jacobi_threads.o: src/jacobi_threads.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/jacobi_processes.o: src/jacobi_processes.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/main_seq.o: src/main_seq.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/main_threads.o: src/main_threads.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

src/main_processes.o: src/main_processes.c include/jacobi.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o JacobiSec JacobiHilos JacobiProc

.PHONY: all clean
