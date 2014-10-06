#!/bin/bash

ADDRESS=$1

# run RDMA WRITE
#latency
./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
sleep 1

#bandwidth
./rdma -v w -b 10 -t 5000 $ADDRESS >> write.dat
sleep 1
./rdma -v w -b 15 -t 1000 $ADDRESS >> write.dat
sleep 1
./rdma -v w -b 20 -t 500  $ADDRESS >> write.dat
sleep 1
./rdma -v w -b 25 -t 100 $ADDRESS >> write.dat
sleep 1

# run SEND
#latency
./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
sleep 1

#bandwidth
./rdma -v r -b 10 -t 5000 $ADDRESS >> send.dat
sleep 1
./rdma -v r -b 15 -t 1000 $ADDRESS >> send.dat
sleep 1
./rdma -v r -b 20 -t 500  $ADDRESS >> send.dat
sleep 1
./rdma -v r -b 25 -t 100 $ADDRESS >> send.dat
sleep 1

exit


# run RDMA READ
#latency
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
sleep 1
#bandwidth
./rdma -v r -b 10 -t 5000 $ADDRESS >> read.dat
sleep 1
./rdma -v r -b 15 -t 1000 $ADDRESS >> read.dat
sleep 1
./rdma -v r -b 20 -t 500 $ADDRESS >> read.dat
sleep 1
./rdma -v r -b 25 -t 100 $ADDRESS >> read.dat
sleep 1

