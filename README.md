# Main_MiSTer

Goal of this is to sort out container building for 

# Development Setup: Boot2Docker, Windows 7

- Download and install Docker - you won't be able to do Docker for Windows unless you are on a newer version of the OS.
- Once installed and tested, ssh into your boot2docker guest machine
- Issue this command to pull down a working linaro toolchain:
```
docker pull wsbu/toolchain-linaro
```
- Issue this command to run a bash on the linaro container and mount the repo root folder inside the container at /source:
```
docker run -ti -v <absolute path to Main_MiSTer>:/source wsbu/toolchain-linaro bash
```
- Go into the /source folder
```
cd /source
```
- Make MiSTer
```
root@d6f7eba5be6f:/source# make
sxmlc.c
ini_parser.cpp
minimig_hdd.cpp
cfg.cpp
spi.cpp
user_io.cpp
archie.cpp
st_ikbd.cpp
tzx2wav.cpp
x86.cpp
battery.cpp
hardware.cpp
st_tos.cpp
input.cpp
minimig_boot.cpp
brightness.cpp
DiskImage.cpp
file_io.cpp
fpga_io.cpp
minimig_config.cpp
sharpmz.cpp
minimig_fdd.cpp
menu.cpp
osd.cpp
main.cpp
MiSTer
```
