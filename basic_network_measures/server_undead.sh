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
printTableHeader(){ echo -e "threads\tbuffer\tbw\tlat\tucpu\tscpu\tucpu_s\tscpu_s"  ;}

trap ctrl_c SIGINT

EXEC=$1
DATE=`date | sed 's/ /_/g'`
DIR='res'

if [ "${EXEC}" = "" ]
then
    cecho "Error: please specify the executable" $red
    exit
fi


# GET NAME OF FILE TO WRITE TO 
cecho "> Specify name of file (defaults to /dev/null)" $white
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
        FILEPATH="/dev/null"
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


if  ! [ -d "$DIR" ] 
then
    mkdir $DIR
fi

touch "$FILEPATH"
printTableHeader | tee -a $FILEPATH

while true 
do
    ./$EXEC | tee -a $FILEPATH
done
