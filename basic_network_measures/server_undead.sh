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
    echo -e "$color $message"
    tput sgr0
    return
}

ctrl_c(){
    make clean
    exit
}

printbwheader() { echo -e "#bytes\titer.\tAvg. BW\tUCPU%\tSCPU%"; }

trap ctrl_c SIGINT

EXEC=$1
DATE=`date | sed 's/ /_/g'`

if [ "${EXEC}" = "" ]
then
    cecho "Error: please specify the executable" $red
    exit
fi


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
            FILEPATH="$DIR/$FILENAME"
            cecho "> experiment will be stored in '$FILEPATH'" $green
            break
        fi
    else
        FILENAME="${EXEC}_${DATE}_server"
        FILEPATH="$DIR/$FILENAME"
        cecho "> defaulting to: $FILEPATH" $green
        break
    fi
done

make clean
cecho "> compiling executable" $green
make $EXEC &> /dev/null
if [ -x $EXEC ]
then
    cecho "> executable compiled"  $green 
else
    cecho "> executable could not be compiled. exiting..." $red
    exit 1
fi

touch "$FILEPATH"
printbwheader | tee $FILEPATH

while true 
do
    ./$EXEC | tee -a $FILEPATH
done
