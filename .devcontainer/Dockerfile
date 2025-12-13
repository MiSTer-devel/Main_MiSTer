FROM ubuntu:22.04
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y git make wget xz-utils && \
    wget -qO- "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz?revision=d0b90559-3960-4e4b-9297-7ddbc3e52783&rev=d0b9055939604e4b92977ddbc3e52783&hash=0074C1529DE90C98726B80ED3EE0776C" | tar -xJ && \
    mv gcc-arm-* /usr/local/bin && \
    printf 'export PATH=$PATH:%s\n' /usr/local/bin/gcc-arm-*/bin >> ~/.bashrc && \
    apt-get autoremove && apt-get clean && \
    rm -rf ~/.cache /tmp/*
