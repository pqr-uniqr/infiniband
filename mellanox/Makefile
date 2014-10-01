CFLAGS= -O6 
CFLAGS= -g
CC=gcc
LD=gcc 
NAME=rdma_rc_example
LIB=-libverbs

all:	example

example: rdma_rc_example.c
	${CC} ${CFLAGS} -m64 -c ${NAME}.c
	${LD} ${NAME}.o ${LIB} -o ${NAME} 

clean:
	rm -f ${NAME} ${NAME}.o

