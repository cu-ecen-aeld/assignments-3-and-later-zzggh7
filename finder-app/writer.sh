#!/bin/sh

#echo "Number of parameters: $#"


if [ $# -eq 2 ]
then
    writefile=$1
    writestr=$2

    mkdir -p "$(dirname "$writefile")" || exit 1
    echo "$writestr" > "$writefile" || exit 1

else
    echo "Invalid number of parameters ($#)"
    echo "You need to provide 2 parameters:"
    echo "1) A full path to a file including filename"
    echo "2) Text string to be written to the file"
    exit 1
fi