#!/bin/bash

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
printheader() { echo -e "#bytes\titer.\tAvg. BW\tUCPU%\tSCPU%"; }
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
        FILENAME="${EXEC}_${MEASURE}_${DATE}"
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


if [ "${MEASURE}" = 'bw' ]
then
    # GET XFER SIZE
    cecho "> I will test transfer sizes from 2^1 to 2^x. Specify x. (max 29)" $white
    while read POW; do
        if [ "0$POW" -gt 29  ] || [ -z "${POW}" ]
        then
            cecho "> try again (max 29)" $red
        else
            bytes=`echo "2^$POW" | bc`
            cecho "> up to $bytes bytes" $green
            break
        fi
    done 

    # GET NUMBER OF ITERATIONS
    cecho "> I will run y many iterations for each transfer size. Specify y (max 100000)" $white
    while read ITER; do
        if [ "0$ITER" -gt 100000 ] || [ -z "${ITER}" ]
        then 
            cecho "> try again (max 100000)" $red
        else
            cecho "> $ITER iterations each" $green
            break
        fi
    done
elif [ "${MEASURE}" = 'lat' ]
then
    cecho "> Latency test: will test transfer sizes from 1 byte, 2 bytes, ...., 32 bytes" $white
    cecho "> I will run 10^y many iterations for each transfer sizes. Specify y (max 10)" $white
    while read ITER; do 
        if [ -z "${ITER}" ] || [ "0$ITER" -gt 10 ]
        then
            cecho "> try again(max 10,000,000,000)" $red
        else
            cecho "> 10^$ITER iterations each" $green
            break
        fi
    done
fi

# GET NUMBER OF THREADS
cecho "> I will run the experiment specified above for each of the thread. Specify number of threads" $white
while read THREAD; do
    if [ "0$THREAD" -gt 30 ] || [ -z "${THREAD}" ]
    then 
        cecho "> try again (max 30)" $red
    else
        cecho "> $THREAD threads" $green
        break
    fi
done

# CHECK IF RESULT DIRETORY EXISTS 
if  ! [ -d "$DIR" ] 
then
    mkdir $DIR
fi

# if this is called on an existing file (such as /dev/null), won't really hurt
touch "$FILEPATH"
echo "#$EXEC experiment to measure $MEASURE: " > $FILEPATH
echo "Up to 2^$POW bytes, each $ITER iterations on $THREAD threads (server addr: $ADDR)" >> $FILEPATH
echo "#* to reproduce this result, use $GITVER *" >> $FILEPATH

if [ "$EXEC" = 'rdma' ] || [ "$EXEC" = 'rdma_dbg' ]; then

    # GET VERB FOR RDMA
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
    echo "#verb: $OP"  >> $FILEPATH
    printheader >> $FILEPATH
    cecho "starting experiment..." $green
    cecho "STDERR: " $red

    # TODO hypothetical code
    echo "WARNING: YOU'RE RUNNING HYPOCODE"
    # RUN RDMA EXPERIMENT
    for i in `seq 1 $POW`; do
      ./$EXEC -v $OP -i $ITER -b $i -t $THREAD -m $MEASURE $ADDR >> $FILEPATH
      sleep 0.1
    done

else
    printheader >> $FILEPATH
    cecho "starting experiment..." $green
    cecho "STDERR: " $red

    # TODO hypothetical code
    echo "WARNING: YOU'RE RUNNING HYPOCODE"
    # RUN IP EXPERIMENT
    for i in `seq 1 $POW`; do
        ./$EXEC -b $i -i $ITER -t $THREAD -m $MEASURE $ADDR >> $FILEPATH
      sleep 0.1
    done
fi

make clean

exit 0

