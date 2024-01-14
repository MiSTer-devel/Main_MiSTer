FROM ubuntu:kinetic

RUN apt-get update -y && apt-get upgrade -y
RUN apt-get install build-essential git libncurses-dev flex bison openssl libssl-dev dkms libelf-dev \
     libudev-dev libpci-dev libiberty-dev autoconf liblz4-tool bc curl gcc git libssl-dev libncurses5-dev \ \
     lzop make u-boot-tools libgmp3-dev libmpc-dev -y
RUN sed -Ei "s/^# deb-src /deb-src /" /etc/apt/sources.list && \
    apt-get update -y && \
    ln -snf /usr/share/zoneinfo/$CONTAINER_TIMEZONE /etc/localtime && echo $CONTAINER_TIMEZONE > /etc/timezone && \
    apt-get build-dep linux -y

WORKDIR /opt
RUN apt-get install wget -y && \
    wget -c https://releases.linaro.org/components/toolchain/binaries/6.5-2018.12/arm-linux-gnueabihf/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz
RUN cat gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf.tar.xz | tar xJ

RUN useradd -m -u 1000 build
USER build

ENV CROSS_COMPILE=/opt/gcc-linaro-6.5.0-2018.12-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
ENV ARCH=arm
ENV LOCALVERSION=-MiSTer

WORKDIR /build
