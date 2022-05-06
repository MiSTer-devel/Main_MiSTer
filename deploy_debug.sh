#! /bin/sh

# defines
TARGET=mister-dev
REMOTEPATH=/media/fat/MiSTer.elf
EXECUTABLE=MiSTer.elf

# remove old executable on target
ssh root@$TARGET "rm $REMOTEPATH; killall -q MiSTer.elf"
# copy over new executable
scp $EXECUTABLE root@$TARGET:$REMOTEPATH
