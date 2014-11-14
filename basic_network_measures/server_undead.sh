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


EXEC=$1

trap ctrl_c SIGINT


if [ "${EXEC}" = "" ]
then
    cecho "Error: please specify the executable" $red
else
    make clean
    cecho "> compiling executable" $green
    make $EXEC

    if [ -x $EXEC ]
    then
        cecho "> executable compiled"  $green 
    else
        cecho "> executable could not be compiled. exiting..." $red
        exit 1
    fi

    while true 
    do
        ./$EXEC
    done
fi
