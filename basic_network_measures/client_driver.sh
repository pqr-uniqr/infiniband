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

printbwheader() { echo -e "#thread\t#bytes\titer.\tAvg. BW\tUCPU%\tSCPU%\tUCPU%(s)\tSCPU%(s)"; }
printlatheader() { echo -e "#thread\t#bytes\titer.\tlatency"; }

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
cecho "> Specify name of file (defaults to <experiment type>_<date/time>)" $white
while read FILENAME; do
    if [ -n "${FILENAME}" ]
    then
        # IF SOMETHING LIKE /dev/null
        if [ `expr index ${FILENAME} /` = 1]
        then
            FILEPATH="${FILENAME}"
            break
        # IF FILE ALREADY EXISTS 
        elif [ -e "$DIR/$FILENAME" ]
        then
            cecho "> file already exists" $red
        # OTHERWISE, VALID NAME
        else
            FILEPATH="$DIR/$FILENAME"
            cecho "> experiment will be stored in '$FILEPATH'" $green
            break
        fi
    else
        # GO WITH DEFAULT
        cecho "> defaulting to something like: ${EXEC}_${MEASURE}_${DATE}" $green
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

# MULTITHREADED?
cecho "> It this a concurrency experiment? (yes/no)" $white
while read MTHREAD; do
    if [ "${MTHREAD}" = 'yes' ] || [ "${MTHREAD}" = 'y' ]
    then
        cecho "> multi-threaded experiment" $green
        MTHREAD=1
        break
    elif [ "${MTHREAD}" = 'no' ] || [ "${MTHREAD}" = 'n' ]
    then
        cecho "> single-threaded experiment" $green
        MTHREAD=0
        break
    else
        cecho "> yes or no" $red
        break
    fi
done

if [ $MTHREAD -gt 0 ]; then
    # GET FIXED XFER SIZE LIMIT  --> POW (recommended: 14)
    cecho "> transfer buffer size will be fixed to 2^x. Specify x (max 29, 12 recommended)" $white
    while read POW; do
        if [ "0$POW" -gt 29  ] || [ -z "${POW}" ]
        then
            cecho "> try again (max 29)" $red
        else
            bytes=`echo "2^$POW" | bc`
            cecho "> transfer size will be $bytes bytes" $green
            break
        fi
    done 

    # GET ITERATIONS --> ITER (recommended: 1000)
    cecho "> I will run 10^y many iterations per thread. Specify y (max 10, 3 recommended)" $white
    while read ITER; do 
        if [ -z "${ITER}" ] || [ "0$ITER" -gt 10 ]
        then
            cecho "> try again(max 10,000,000,000)" $red
        else
            ITER=`echo "10^$ITER" | bc`
            cecho "> $ITER iterations each" $green
            break
        fi
    done

    # GET THREAD NUMBER LIMIT --> THREAD (recommended: 14)
    cecho "> I will run the above experiment with 2^1 to 2^z threads. Specify z (max 20, 14 recommended)" $white
    while read THREAD; do
        if [ -z "${THREAD}" ] || [ "0$THREAD" -gt 20 ]
        then
            cecho "> try again (max 20)" $red
        else
            MAXTHREAD=`echo "2^$THREAD" | bc`
            cecho "> up to $MAXTHREAD threads" $green
            break
        fi
    done
else
    MAXTHREAD=1
    # GET XFER SIZE LIMIT --> POW (recommended: 24)
    cecho "> I will test transfer sizes from 2^1 to 2^x. Specify x. (max 29, 24 recommended)" $white
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

    # GET ITERATIONS --> ITER (recommended: 10000)
    cecho "> I will run 10^y many iterations for each transfer sizes. Specify y (max 10, 5 recommended)" $white
    while read ITER; do 
        if [ -z "${ITER}" ] || [ "0$ITER" -gt 10 ]
        then
            cecho "> try again(max 10,000,000,000)" $red
        else
            ITER=`echo "10^$ITER" | bc`
            cecho "> $ITER iterations each" $green
            break
        fi
    done
fi




# CHECK IF RESULT DIRETORY EXISTS 
if  ! [ -d "$DIR" ] 
then
    mkdir $DIR
fi


FILEHEADER="# $EXEC experiment to measure $MEASURE:\n" 
FILEHEADER="${FILEHEADER}# Up to 2^$POW bytes, each $ITER iterations with up to $MAXTHREAD threads (server addr: $ADDR)\n" 
FILEHEADER="${FILEHEADER}# to reproduce this result, use $GITVER *\n"

# BRANCH INTO RDMA AND IP SPECIFIC SETTINGS

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
   
    # FINALIZE FILE NAME
    if [ -z "${FILEPATH}" ]
    then
        FILENAME="${EXEC}_${MEASURE}_${OP}"
        if [ $MTHREAD -gt 0 ]; then
            FILENAME="${FILENAME}_mthread"
        fi
        FILENAME="${FILENAME}_${DATE}"
        FILEPATH="$DIR/$FILENAME"
    fi


    touch "$FILEPATH"

    echo -e $FILEHEADER | tee -a $FILEPATH

    if [ "${MEASURE}" = 'bw' ]
    then
        printbwheader | tee -a $FILEPATH
    elif [ "${MEASURE}" = 'lat' ]
    then
        printlatheader | tee -a $FILEPATH
    fi

    cecho "starting experiment..." $green
    cecho "STDERR: " $red

    if [ $MTHREAD -gt 0 ]; then
        for i in `seq 1 $THREAD`; do
            threads=`echo "2^$i" | bc`
            ./$EXEC -v $OP -i $ITER -b $POW -t $threads -m $MEASURE $ADDR | tee -a $FILEPATH
            sleep 0.1
        done
    else
        for i in `seq 1 $POW`; do
            ./$EXEC -v $OP -i $ITER -b $i -t 1 -m $MEASURE $ADDR | tee -a $FILEPATH
            sleep 0.1
        done
    fi


    # RUN RDMA EXPERIMENT

else
    cecho "> Please specify link type (eth for ethernet, ib for infiniband)" $white
    while read LT; do 
        if  [ -n "${LT}" ]; then 
            if [ "$LT" != 'ib' ] && [ "$LT" != 'eth' ] ; then
                cecho "> try again (eth for ethernet, ib for infiniband)" $red
            else
                cecho "> $LT received" $green
                break
            fi
        else
            cecho "> try again" $red
        fi
    done


    # FINALIZE FILE NAME
    if [ -z "${FILEPATH}" ]
    then
        FILENAME="${EXEC}_${MEASURE}_${LT}"
        if [ $MTHREAD -gt 0 ]; then
            FILENAME="${FILENAME}_mthread"
        fi
        FILENAME="${FILENAME}_${DATE}"
        FILEPATH="$DIR/$FILENAME"
    fi

    touch "$FILEPATH"
    echo -e $FILEHEADER | tee -a $FILEPATH

    if [ "${MEASURE}" = 'bw' ]
    then
        printbwheader | tee -a $FILEPATH
    elif [ "${MEASURE}" = 'lat' ]
    then
        printlatheader | tee -a $FILEPATH
    fi

    cecho "starting experiment..." $green
    cecho "STDERR: " $red


    if [ $MTHREAD -gt 0 ]; then
        for i in `seq 1 $THREAD`; do
            threads=`echo "2^$i" | bc`
            ./$EXEC -b $POW -i $ITER -t $threads -m $MEASURE $ADDR | tee -a $FILEPATH
            sleep 0.1
        done
    else
        for i in `seq 1 $POW`; do
            ./$EXEC -b $i -i $ITER -t 1 -m $MEASURE $ADDR | tee -a $FILEPATH
          sleep 0.1
        done
    fi

    # RUN IP EXPERIMENT
fi

make clean

exit 0

