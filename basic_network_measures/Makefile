CFLAGS= -g -std=c99 -D_GNU_SOURCE -pthread -L /lib64 
CC=gcc
LD=gcc 
LIB= -libverbs -lm -lpthread -lnuma
LIB_SLAB= -libverbs -lm -lpthread

CPPC=g++
CPPLIB= IntelPCM/pci.cpp IntelPCM/msr.cpp IntelPCM/client_bw.cpp IntelPCM/cpucounters.cpp -lrt

all: rdma

rdma: rdma.c
	${CC} ${CFLAGS} -m64 -c rdma.c -DNUMA
	${LD} rdma.o ${LIB} -o rdma
	rdma_slab: rdma.c
	${CC} ${CFLAGS} -m64 -c rdma.c
	${LD} rdma.o ${LIB_SLAB} -o rdma

rdma_dbg: rdma.c  
	${CC} ${CFLAGS} -m64 -c rdma.c -DDEBUG -DNUMA
	${LD} rdma.o ${LIB} -o rdma_dbg

ip: ip.c ip.h wrapper.cpp
	${CC} ${CFLAGS} -m64 -c ip.c -DNUMA
	${CPPC} -c wrapper.cpp
	g++ ip.o ${LIB} ${CPPLIB} wrapper.o -o ip

ip_slab: ip.c ip.h wrapper.cpp
	${CC} ${CFLAGS} -m64 -c ip.c
	${CPPC} -c wrapper.cpp
	g++ ip.o ${LIB_SLAB} ${CPPLIB} wrapper.o -o ip

ip_dbg: ip.c ip.h wrapper.cpp
	${CC} ${CFLAGS} -m64 -c ip.c -DDEBUG -DNUMA
	${CPPC} -c wrapper.cpp
	g++ ip.o ${LIB} ${CPPLIB} wrapper.o -o ip_dbg

clean:
	rm -f *.o main test ip ip_dbg rdma rdma_dbg

