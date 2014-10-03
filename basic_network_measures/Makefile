CFLAGS= -O6 
CFLAGS= -g -Wall
CC=gcc
LD=gcc 
NAME=rdma
LIB=-libverbs -lm

all: rdma

rdma: rdma.c
	${CC} ${CFLAGS} -m64 -c ${NAME}.c
	${LD} ${NAME}.o ${LIB} -o ${NAME} 

debug: rdma.c 
	${CC} ${CFLAGS} -m64 -c ${NAME}.c -DDEBUG
	${LD} ${NAME}.o ${LIB} -o ${NAME} 


clean:
	rm -f ${NAME} ${NAME}.o

