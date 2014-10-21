#!/bin/bash

black='\E[30;200m'
red='\E[31;200m'
green='\E[32;200m'
yellow='\E[33;200m'
blue='\E[34;200m'
magenta='\E[35;200m'
cyan='\E[36;200m'
white='\E[37;200m'

cecho ()                     # Color-echo.
                             # Argument $1 = message
                             # Argument $2 = color
{
local default_msg="No message passed."
                             # Doesn't really need to be a local variable.

message=${1:-$default_msg}   # Defaults to default message.
color=${2:-$black}           # Defaults to black, if not specified.

    echo -e "$color$message"
    tput sgr0

return
}  


EXEC=$1

if [ "$EXEC" != 'ip' -a "$EXEC" != 'rdma' ]
then
    cecho "Error: please specify the executable (ip or rdma)" $red
else
    if [ -x $EXEC ]
    then
        cecho "> executable present. "  $green 
    else
        cecho "> executable not present--compiling" $red
        make $EXEC
        if [ -x $EXEC ]
        then
            cecho "> executable compiled." $green
        else
            cecho "> executable could not be compiled. exiting..." $red
            exit 1
        fi
    fi

    cecho "> Specify address of the server" $white
    read ADDR
    cecho "> $ADDR it is." $green #TODO bheck

    cecho "> I will test transfer sizes from 2^1 to 2^x. Specify x. (max 29)" $white
    while true;
    do
        read POW #TODO nulcheck
        if [ $POW -gt 29  ]
        then
            cecho "> try again (max 29)" $red
        else
            bytes=`echo "2^$POW" | bc`
            cecho "> up to $bytes bytes" $green
            break
        fi
    done 

    cecho "> i will run y many iterations for each transfer size. Specify y (max 100000)" $white
    while true;
    do 
        read ITER #TODO nullcheck
        if [ $ITER -gt 100000 ]
        then 
            cecho "> try again (max 100000)" $red
        else
            cecho "> $ITER iterations each" $green
            break
        fi
    done

    if [ "$EXEC" = 'rdma' ]
    then
        cecho "> Please specify the operation ('r' for RDMA READ, 'w' for RDMA WRITE, 's' for IB SEND)" $white
        while true;
        do 
            read OP #TODO nullcheck 
            if [ "$OP" != 'r' ] && [ "$OP" != 'w' ] && [ "$OP" != 's' ]
            then
                cecho "> try again " $red
            else
                break
            fi
        done


        echo -e "#bytes\t#iterations\tPeak BW\tAvg. BW"
        for i in `seq 1 $POW`;
        do
          ./rdma -v $VERB -i $ITER -b $i $ADDRESS
          sleep 1
        done
    else


        echo -e "#bytes\t#iterations\tPeak BW\tAvg. BW"
        for i in `seq 1 $POW`;
        do
            ./ip -b $i -i $ITER $ADDR
          sleep 1
        done
    fi



fi

exit 0

