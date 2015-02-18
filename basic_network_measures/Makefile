CFLAGS= -g -std=c99 -D_GNU_SOURCE -pthread -L /lib64 
CC=gcc
LD=gcc 
LIB= -libverbs -lm -lpthread -lnuma
all: rdma

rdma: rdma.c
	${CC} ${CFLAGS} -m64 -c rdma.c
	${LD} rdma.o ${LIB} -o rdma

rdma_dbg: rdma.c  
	${CC} ${CFLAGS} -m64 -c rdma.c -DDEBUG
	${LD} rdma.o ${LIB} -o rdma_dbg

ip: ip.c  ip.h
	${CC} ${CFLAGS} -m64 -c ip.c
	${LD} ip.o ${LIB} -o ip

ip_dbg: ip.c  ip.h
	${CC} ${CFLAGS} -m64 -c ip.c -DDEBUG
	${LD} ip.o ${LIB} -o ip_dbg

mbench: mbench.c
	${CC} ${CFLAGS} -m64 -c mbench.c 
	${LD} mbench.o ${LIB} -o mbench

clean:
	rm -f rdma rdma_dbg ip ip_dbg mbench ip.o rdma.o mbench.o
