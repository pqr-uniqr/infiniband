CFLAGS= -O3 -g -Wall -std=c99 -D_GNU_SOURCE -pthread
CC=gcc
LD=gcc 
LIB=-libverbs -lm 
all: rdma

rdma: rdma.c get_clock.c get_clock.h
	${CC} ${CFLAGS} -m64 -c rdma.c
	${LD} rdma.o ${LIB} -o rdma

rdma_dbg: rdma.c  get_clock.c get_clock.h 
	${CC} ${CFLAGS} -m64 -c rdma.c -DDEBUG
	${LD} rdma.o ${LIB} -o rdma_dbg

ip: ip.c  ip.h
	${CC} ${CFLAGS} -m64 -c ip.c
	${LD} ip.o ${LIB} -o ip

ip_dbg: ip.c  ip.h
	${CC} ${CFLAGS} -m64 -c ip.c -DDEBUG
	${LD} ip.o ${LIB} -o ip_dbg


clean:
	rm -f rdma rdma.o ip ip.o ip_dbg rdma_dbg

