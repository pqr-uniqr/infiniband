#!/bin/bash

ADDRESS=$1
VERB=$2
CRITERIA=$3
UPTO=23


for i in `seq 1 $UPTO`;
do
  ./rdma -v $VERB -i 1000 -b $i -c $CRITERIA $ADDRESS
  sleep 1
done
