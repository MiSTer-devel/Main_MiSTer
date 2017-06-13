#!/bin/bash

# make script fail if any command failed,
# so we don't need to check the exit status of every command.
set -e
set -o pipefail
make

set +e
plink root@192.168.1.75 -pw 1 'killall MiSTer'

set -e
ftp -n <<EOF
open 192.168.1.75
user root 1
binary
put MiSTer /media/fat/MiSTer
EOF

plink root@192.168.1.75 -pw 1 'PATH=/media/fat:$PATH;MiSTer >/dev/ttyS0 2>/dev/ttyS0 </dev/null &'
