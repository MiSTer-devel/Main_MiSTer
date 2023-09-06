#!/bin/bash

# NOTE: This script is meant to be run under Ubuntu Linux,
# including under WSL on Windows.

# Make script fail if any command failed, so we don't need
# to check the exit status of every command.
set -e
set -o pipefail

# Override this default value by creating a text file named 'host'
# in this folder containing the IP address of your MiSTer.
HOST=192.168.1.75
[ -f host ] && HOST=$(cat host)

echo "Starting build..."

make

set +e
echo y|plink root@$HOST -pw 1 'killall MiSTer'

set -e
ftp -n <<EOF
open $HOST
user root 1
passive
binary
put MiSTer /media/fat/MiSTer
EOF

plink root@$HOST -pw 1 -batch 'sync;PATH=/media/fat:$PATH;MiSTer >/dev/ttyS0 2>/dev/ttyS0 </dev/null &'
