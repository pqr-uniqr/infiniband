#!/usr/bin

# run RDMA READ

#latency
./rdma -v r -t 10000 -b 0 >> read.dat
echo "1"
#bandwidth
./rdma -v r -b 10 -t 5000 >> read.dat
echo "2"
./rdma -v r -b 15 -t 1000 >> read.dat
echo "3"
./rdma -v r -b 20 -t 500  >> read.dat
echo "4"
./rdma -v r -b 25 -t 100 >> read.dat
echo "5"

# run RDMA WRITE
#latency
./rdma -v r -t 10000 -b 0 >> write.dat
echo "6"
#bandwidth
./rdma -v r -b 10 -t 5000 >> write.dat
echo "7"
./rdma -v r -b 15 -t 1000 >> write.dat
echo "8"
./rdma -v r -b 20 -t 500  >> write.dat
echo "9"
./rdma -v r -b 25 -t 100 >> write.dat
echo "10"

# run SEND
#latency
./rdma -v r -t 10000 -b 0 >> send.dat
echo "11"
#bandwidth
./rdma -v r -b 10 -t 5000 >> send.dat
echo "12"
./rdma -v r -b 15 -t 1000 >> send.dat
echo "13"
./rdma -v r -b 20 -t 500  >> send.dat
echo "14"
./rdma -v r -b 25 -t 100 >> send.dat
echo "15"

