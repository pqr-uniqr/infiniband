#!/bin/bash


# TODO for now

black='\E[30;200m'
red='\E[31;200m'
green='\E[32;200m'
yellow='\E[33;200m'
blue='\E[34;200m'
magenta='\E[35;200m'
cyan='\E[36;200m'
white='\E[37;200m'

cecho(){
    local default_msg="No message passed."
    message=${1:-$default_msg}  
    color=${2:-$black}
    echo -e "$color$message"
    tput sgr0
    return
}  
printbwheader() { echo -e "#bytes\titer.\tAvg. BW\tUCPU%\tSCPU%"; }
printlatheader() { echo -e "#bytes\titer.\tlatency"; }
ctrl_c(){
    make clean
    exit
}
trap ctrl_c SIGINT


DATE=`date | sed 's/ /_/g'`
GITVER=`git show | grep "commit" -m 1`
DIR='res'
EXEC=$1
DBG=$2


if [ "${EXEC}" = "" ]
then
    cecho "Error: please specify the executable" $red
    exit
fi


# CORDIAL MESSAGE AND SOME INFO
cecho "##########################" $cyan
cecho "> hi there" $cyan
cecho "> $EXEC experiment (date: $DATE, using $GITVER)" $cyan
cecho "##########################\n" $cyan
sleep 0.5


# RECOMPILE EXECUTABLE
make clean
cecho "> compiling executable..."  $green
make $EXEC &> /dev/null
if [ -x $EXEC ]
then
    cecho "> executable compiled." $green
else
    cecho "> executable could not be compiled. exiting..." $red
    exit 1
fi


# WHAT TO MEASURE
cecho "> what are we measuring? (latency, bandwidth)" $white 
while read MEASURE; do
    if [ "${MEASURE}" = 'b' ] || [ "${MEASURE}" = 'bw' ] || [ "${MEASURE}" = 'bandwidth' ]
    then
        cecho "> bandwidth selected" $green
        MEASURE='bw'
        break
    elif [ "${MEASURE}" = 'l' ] || [ "${MEASURE}" = 'lat' ] || [ "${MEASURE}" = 'latency' ]
    then
        cecho "> latency selected" $green
        MEASURE='lat'
        break
    else
        cecho "> no such thing. try again ('b' or 'bw' for bandwidth, 'l' or 'lat' for latency)" $red
    fi
done


# GET NAME OF FILE TO WRITE TO 
cecho "> Specify name of file (defaults to date/time)" $white
while read FILENAME; do
    if [ -n "${FILENAME}"  ]
    then
        if [ -e "$DIR/$FILENAME" ]
        then
            cecho "> file already exists" $red
        elif [ `expr index ${FILENAME} /` = 1 ] 
        then
            # we can order redirection to command line tools (e.g. /dev/null)
            FILEPATH="${FILENAME}"
            break
        else
            FILENAME="${EXEC}_$FILENAME_${MEASURE}"
            FILEPATH="$DIR/$FILENAME"
            cecho "> experiment will be stored in '$FILEPATH'" $green
            break
        fi
    else
        FILENAME="${EXEC}_${MEASURE}_${DATE}_mthread"
        FILEPATH="$DIR/$FILENAME"
        cecho "> defaulting to: $FILEPATH" $green
        break
    fi
done

# GET SERVER ADDR
cecho "> Specify address of the server" $white
while read ADDR; do 
    if [ -z "${ADDR}" ]
    then
        cecho "> try again " $red
    else
        cecho "> $ADDR it is." $green #TODO check
        break
    fi
done


cecho "> select buffer size (recommended: select size that gives maximum BW with single thread)" $white
while read POW; do
    if [ "0$POW" -gt 29 ] || [ -z "${POW}" ]
    then
        cecho "> try again(max 29)" $red
    else
        bytes=`echo "2^$POW" | bc`
        cecho "> up to $bytes bytes " $green
        break
    fi
done

cecho "> I will run around y total iterations over all the threads. Specify y (10000 recommended for over Ib, 5000 recommended for over Ethernet) " $white
while read ITER; do
    if [ "0$ITER" -gt 100000 ] || [ -z "${ITER}" ]
    then 
        cecho "> try again (max 100000)" $red
    else
        cecho "> $ITER iterations total" $green
        break
    fi
done

cecho "> I will run the above experiment using 1 thread to 2^z threads. Specify z (recommended 10)"
while read THREAD:do
    if [ "0$THREAD" -gt 12 ] || [ -z "${THREAD}" ]
    then
        cecho "> try again (max 12)" $red
    else
        NUMTHREAD=`echo "2^$THREAD" | bc`
        cecho "> up to $NUMTHREAD threads (for each thread, $ITER iterations)" $green
        break
    fi
done

# CHECK IF RESULT DIRETORY EXISTS 
if  ! [ -d "$DIR" ] 
then
    mkdir $DIR
fi


touch "$FILEPATH"
echo "#$EXEC experiment to measure $MEASURE: " | tee $FILEPATH
echo "#2^$POW buffer size, up to 2^$THREAD number of threads with total $ITER iterations (server addr: $ADDR)" | tee -a $FILEPATH
echo "#* to reproduce this result, use $GITVER" | tee -a $FILEPATH

if [ "$EXEC" = 'rdma' ]
then
    cecho "> Please specify the operation ('r' for RDMA READ, 'w' for RDMA WRITE, 's' for IB SEND)" $white
    while read OP; do 
        if  [ -n "${OP}" ]; then 
            if [ "$OP" != 'r' ] && [ "$OP" != 'w' ] && [ "$OP" != 's' ] ; then
                cecho "> try again " $red
            else
                cecho "> $OP received" $green
                break
            fi
        fi
    done
    echo "#verb: $OP"  | tee -a $FILEPATH

    if [ "${MEASURE}" = 'bw' ]
    then
        printbwheader | tee -a $FILEPATH
    elif [ "${MEASURE}" = 'lat' ]
    then
        printlatheader | tee -a $FILEPATH
    fi

    cecho "starting experiment..." $green
    cecho "STDERR: " $red

    # RUN RDMA EXPERIMENT
    for i in `seq 1 $THREAD`; do
        NUMTHREAD=`echo "2^$i" | bc`
        ITER=`echo "$ITER / $NUMTHREAD" | bc` 
        ./$EXEC -v $OP -i $ITER -b $POW -t $NUMTHREAD -m $MEASURE $ADDR | tee -a $FILEPATH
        sleep 0.1
    done
else 

else
    if [ "${MEASURE}" = 'bw' ]
    then
        printbwheader | tee -a $FILEPATH
    elif [ "${MEASURE}" = 'lat' ]
    then
        printlatheader | tee -a $FILEPATH
    fi

    cecho "starting experiment..." $green
    cecho "STDERR: " $red

    # RUN IP EXPERIMENT
    for i in `seq 1 $THREAD`; do
        NUMTHREAD=`echo "2^$i" | bc`
        ITER=`echo "$ITER / $NUMTHREAD" | bc` 
        ./$EXEC -b $POW -i $ITER -t $NUMTHREAD -m $MEASURE $ADDR | tee -a $FILEPATH
        sleep 0.1
    done
fi

make clean

exit 0


