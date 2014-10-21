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

    echo -e "$color $message"
    tput sgr0

return
}

EXEC=$1


if [ "$EXEC" != 'ip' -a "$EXEC" != 'rdma' ]
then
    cecho "Error: please specify the executable (ip or rdma)" $red
else
    while true 
    do
        `./$EXEC`
    done
fi
