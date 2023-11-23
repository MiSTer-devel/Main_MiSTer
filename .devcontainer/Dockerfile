FROM ubuntu:22.04
RUN apt-get update && apt-get upgrade -y && \
    apt-get install -y make wget xz-utils && \
    wget -qO- "https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf.tar.xz?rev=302e8e98351048d18b6f5b45d472f406&hash=95ED9EEB24EAEEA5C1B11BBA864519B2" | tar -xJ && \
    mv gcc-arm-* /usr/local/bin && \
    printf 'export PATH=$PATH:%s\n' /usr/local/bin/gcc-arm-*/bin >> ~/.bashrc && \
    printf 'export CC=%s\n' /usr/local/bin/gcc-arm-*/bin/arm-none-linux-gnueabihf-gcc >> ~/.bashrc && \
    source ~/.bashrc && \
    apt-get autoremove && apt-get clean && \
    rm -rf ~/.cache /tmp/*
