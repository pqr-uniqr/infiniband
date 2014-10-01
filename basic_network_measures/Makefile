CFLAGS= -O6 
CFLAGS= -g
CC=gcc
LD=gcc 
NAME=rdma
LIB=-libverbs

all:	example

example: rdma.c rdma.h
	${CC} ${CFLAGS} -m64 -c ${NAME}.c
	${LD} ${NAME}.o ${LIB} -o ${NAME} 

clean:
	rm -f ${NAME} ${NAME}.o

