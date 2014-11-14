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
printheader() { echo -e "#bytes\titer.\tAvg. BW\tCPU%"; }
ctrl_c(){
    make clean
    exit
}


DATE=`date | sed 's/ /_/g'`
GITVER=`git show | grep "commit" -m 1`
DIR='res'
EXEC=$1
DBG=$2


if [ "${EXEC}" = "" ]
then
    cecho "Error: please specify the executable" $red
else

    # CORDIAL MESSAGE AND SOME INFO
    cecho "##########################" $cyan
    cecho "> hi there" $cyan
    cecho "> $EXEC experiment (date: $DATE, using $GITVER)" $cyan
    cecho "##########################\n" $cyan

    sleep 0.5


    # RECOMPILE EXECUTABLE
    make clean
    cecho "> compiling executable..."  $green
    make $EXEC
    if [ -x $EXEC ]
    then
        cecho "> executable compiled." $green
    else
        cecho "> executable could not be compiled. exiting..." $red
        exit 1
    fi

    # GET NAME OF FILE TO WRITE TO 
    cecho "> Specify name of file (defaults to date/time)" $white
    while read FILENAME; do
        if [ -n "${FILENAME}"  ]
        then
            if [ -e "$DIR/$FILENAME" ]
            then
                cecho "> file already exists" $red
            else
                FILENAME="${EXEC}_$FILENAME"
                FILEPATH="$DIR/$FILENAME"
                cecho "> experiment will be stored in '$FILEPATH'" $green
                break
            fi
        else
            FILENAME="${EXEC}_$DATE"
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
    cecho "> i will run y many iterations for each transfer size. Specify y (max 100000)" $white
    while read ITER; do
        if [ "0$ITER" -gt 100000 ] || [ -z "${ITER}" ]
        then 
            cecho "> try again (max 100000)" $red
        else
            cecho "> $ITER iterations each" $green
            break
        fi
    done

    # CHECK IF RESULT DIRETORY EXISTS 
    if  ! [ -d "$DIR" ] 
    then
        mkdir $DIR
    fi

    touch "$FILEPATH"
    echo "#$EXEC experiment: Up to 2^$POW bytes, each $ITER iterations (server addr: $ADDR)" > $FILEPATH
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
        cecho "STDERR OUTPUT: " $red

        # RUN RDMA EXPERIMENT
        for i in `seq 1 $POW`; do
          ./$EXEC -v $OP -i $ITER -b $i $ADDR >> $FILEPATH
          sleep 0.1
        done
    else
        printheader >> $FILEPATH
        cecho "starting experiment..." $green
        cecho "STDERR OUTPUT: " $red

        # RUN IP EXPERIMENT
        for i in `seq 1 $POW`; do
            ./$EXEC -b $i -i $ITER $ADDR >> $FILEPATH
          sleep 0.1
        done
    fi

    make clean
fi

exit 0

