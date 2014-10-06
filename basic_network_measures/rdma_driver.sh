#!/bin/bash

ADDRESS=$1

# run RDMA WRITE
#latency
./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
echo "6"
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
echo "6"
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
echo "6"
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
echo "6"
sleep 1

./rdma -v w -t 10000 -b 0 $ADDRESS >> write.dat
echo "6"
sleep 1

#bandwidth
./rdma -v w -b 10 -t 5000 $ADDRESS >> write.dat
echo "7"
sleep 1
./rdma -v w -b 15 -t 1000 $ADDRESS >> write.dat
echo "8"
sleep 1
./rdma -v w -b 20 -t 500  $ADDRESS >> write.dat
echo "9"
sleep 1
./rdma -v w -b 25 -t 100 $ADDRESS >> write.dat
echo "10"
sleep 1

# run SEND
#latency
./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
echo "11"
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
echo "11"
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
echo "11"
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
echo "11"
sleep 1

./rdma -v r -t 10000 -b 0 $ADDRESS >> send.dat
echo "11"
sleep 1

#bandwidth
./rdma -v r -b 10 -t 5000 $ADDRESS >> send.dat
echo "12"
sleep 1
./rdma -v r -b 15 -t 1000 $ADDRESS >> send.dat
echo "13"
sleep 1
./rdma -v r -b 20 -t 500  $ADDRESS >> send.dat
echo "14"
sleep 1
./rdma -v r -b 25 -t 100 $ADDRESS >> send.dat
echo "15"
sleep 1

exit


# run RDMA READ
#latency
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
echo "1"
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
echo "1"
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
echo "1"
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
echo "1"
sleep 1
./rdma -v r -t 10000 -b 0 $ADDRESS  >> read.dat
echo "1"
sleep 1
#bandwidth
./rdma -v r -b 10 -t 5000 $ADDRESS >> read.dat
echo "2"
sleep 1
./rdma -v r -b 15 -t 1000 $ADDRESS >> read.dat
echo "3"
sleep 1
./rdma -v r -b 20 -t 500 $ADDRESS >> read.dat
echo "4"
sleep 1
./rdma -v r -b 25 -t 100 $ADDRESS >> read.dat
echo "5"
sleep 1

