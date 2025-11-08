#!/bin/sh

if [ "$#" -ne 4 ]; then
    echo "Use: $0 address port number maxlen"
    echo "Send <number> messages containing up to <maxlen> random bytes."
    echo "number is how many messages will be sent"
    echo "maxlen is how long randomized messages can get"
    exit 1
fi

addr=$1
port=$2
n=$3
maxlen=$4

nc_eof="" # flag for nc to exit on EOF (none for GNU, -N for BSD)

# check if server is reachable
if nc -z $addr $port > /dev/null 2>&1; then
# check if nc accepts that option
	if nc -zN $addr $port > /dev/null 2>&1; then
		nc_eof="-N"
	fi
else
	echo "Could not establish connection"
	exit 1
fi

if which jot > /dev/null 2>&1; then
    get_random_n="jot -r 1 1 $maxlen"
else
    get_random_n="shuf -i 1-$maxlen -n 1"
fi

for i in $(seq 1 $n); do
    len=$get_random_n
    dd if=/dev/urandom bs=1 count=$len 2> /dev/null | nc $nc_eof $addr $port
done
