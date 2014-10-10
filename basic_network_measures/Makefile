CFLAGS= -O3
CFLAGS= -g 
CC=gcc
LD=gcc 
NAME=rdma
IPNAME=ip
LIB=-libverbs -lm

all: rdma

rdma: rdma.c get_clock.c get_clock.h
	${CC} ${CFLAGS} -m64 -c ${NAME}.c
	${LD} ${NAME}.o ${LIB} -o ${NAME} 

debug: rdma.c  get_clock.c get_clock.h 
	${CC} ${CFLAGS} -m64 -c ${NAME}.c -DDEBUG
	${LD} ${NAME}.o ${LIB} -o ${NAME} 

ip: ip.c 
	${CC} ${CFLAGS} -m64 -c ${IPNAME}.c -DDEBUG
	${LD} ${IPNAME}.o ${LIB} -o ${IPNAME} 

clean:
	rm -f ${NAME} ${NAME}.o ${IPNAME} ${IPNAME}.o

