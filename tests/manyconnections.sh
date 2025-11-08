#!/bin/sh

if [ "$#" -ne 4 ]; then
	echo "Use: $0 address port number timeout"
	echo "Create <number> connections <timeout> seconds long."
	echo "number is how many connections will be created"
	echo "timeout is how long to wait before closing a connection"
	exit 1
fi

addr=$1
port=$2
n=$3
t=$4

# flag for nc to keep connection alive (none on BSD, --no-shutdown on GNU)
nc_noshutdown="--no-shutdown"

# check if server is reachable
if nc -z $addr $port > /dev/null 2>&1; then
# check if nc accepts that option
	if ! timeout 1 nc $nc_noshutdown $addr $port > /dev/null 2>&1; then
		nc_noshutdown=""
	fi
else
	echo "Could not establish connection"
	exit 1
fi

for i in $(seq 1 $n); do
	timeout $t nc $nc_noshutdown $addr $port &
done
