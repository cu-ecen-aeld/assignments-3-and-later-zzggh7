#!/bin/sh

if [ $# -eq 2 ]
then
    filesdir=$1
    searchstr=$2
    #echo "No of params OK"
    #echo "1) ${filesdir}"
    #echo "2) ${searchstr}"

    if [ -d $filesdir ]
    then
        #echo "Directory ${filesdir} exists"
        x=$(find $filesdir -type f | wc -l)
        y=$(grep -r $searchstr $filesdir | wc -l)
        echo "The number of files are ${x} and the number of matching lines are ${y}"
        exit 0
    else
        echo "Directory ${filesdir} DOES NOT exists"
        exit 1
    fi
else
    echo "Invalid number of parameters ($#)"
    echo "You need to provide 2 parameters:"
    echo "1) Path to direstory in the filesystem"
    echo "2) text string to be searchede within these files"
    exit 1
fi

