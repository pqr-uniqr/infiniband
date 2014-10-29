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

printheader() { echo -e "#bytes      #iterations      Peak BW       Avg. BW"; }


DATE=`date | sed 's/ /_/g'`
GITVER=`git show | grep "commit" -m 1`
DIR='res'
EXEC=$1


if [ "$EXEC" != 'ip' -a "$EXEC" != 'rdma' ]
then
    cecho "Error: please specify the executable (ip or rdma)" $red
else

    # CORDIAL MESSAGE AND SOME INFO
    cecho "##########################" $cyan
    cecho "> hi there" $cyan
    cecho "> $EXEC experiment (date: $DATE, using $GITVER)" $cyan
    cecho "##########################\n" $cyan

    sleep 1

    # CHECK IF REQUIRED EXECUTABLE IS PRESENT 
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

    # GET NAME OF FILE TO WRITE TO 
    cecho "> Specify name of file (defaults to date/time)" $white
    while read FILENAME; do
        if [ -n "${FILENAME}"  ]
        then
            if [ -e "$DIR/$FILENAME" ]
            then
                cecho "> file already exists" $red
            else
                FILEPATH="$DIR/$FILENAME"
                cecho "> experiment will be stored in '$FILEPATH'" $green
                break
            fi
        else
            FILENAME="$EXEC_$DATE"
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
    echo "$EXEC experiment: Up to 2^$POW bytes, each $ITER iterations (server addr: $ADDR)" > $FILEPATH
    echo "* to recreate this result, use $GITVER *" >> $FILEPATH

    if [ "$EXEC" = 'rdma' ]; then
        # GET VERB FOR RDMA
        cecho "> Please specify the operation ('r' for RDMA READ, 'w' for RDMA WRITE, 's' for IB SEND)" $white
        while read OP; do 
            if [ -z "${OP}" ] || [ [ "$OP" != 'r' ] && [ "$OP" != 'w' ] && [ "$OP" != 's' ] ]
            then
                cecho "> try again " $red
            else
                break
            fi
        done

        echo "verb: $VERB"  >> $FILEPATH
        printheader >> $FILEPATH
        cecho "STDERR OUTPUT: " $red

        # RUN RDMA EXPERIMENT
        for i in `seq 1 $POW`; do
          ./rdma -v $VERB -i $ITER -b $i $ADDR >> $FILEPATH
          sleep 1
        done
    else
        printheader >> $FILEPATH
        cecho "STDERR OUTPUT: " $red

        # RUN IP EXPERIMENT
        for i in `seq 1 $POW`; do
            ./ip -b $i -i $ITER $ADDR >> $FILEPATH
          sleep 1
        done
    fi

    make clean
fi

exit 0

