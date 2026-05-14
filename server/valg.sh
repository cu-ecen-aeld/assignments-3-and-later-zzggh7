#!/bin/bash
valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all --track-origins=yes --errors-for-leak-kinds=definite --verbose --log-file=/tmp/valgrind-out.txt ./aesdsocket -d
cat /tmp/valgrind-out.txt
echo "==============="
#cat /var/tmp/aesdsocketdata
echo "END"
