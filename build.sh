#!/bin/bash

# create simple text file named 'host' in this folder with IP address of your MiSTer.

HOST=192.168.1.75
[ -f host ] && HOST=$(cat host)

# make script fail if any command failed,
# so we don't need to check the exit status of every command.
set -e
set -o pipefail
make

set +e
plink root@$HOST -pw 1 'killall MiSTer'

set -e
ftp -n <<EOF
open $HOST
user root 1
binary
put MiSTer /media/fat/MiSTer
EOF

plink root@$HOST -pw 1 'sync;PATH=/media/fat:$PATH;MiSTer >/dev/ttyS0 2>/dev/ttyS0 </dev/null &'
