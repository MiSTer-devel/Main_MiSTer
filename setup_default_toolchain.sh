#!/bin/bash

if [ "${BASH_SOURCE[0]}" -ef "$0" ]
then
    echo "This script should be sourced, not executed."
    exit 1
fi

echo "Setting up default toolchain..."

if [ -z "${MISTER_GCC_INSTALL_DIR}" ]; then
	MISTER_GCC_INSTALL_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
fi
if [ -z "${MISTER_GCC_VER}" ]; then
	MISTER_GCC_VER=10.2-2020.11
fi
if [ -z "${MISTER_GCC_HOST_ARCH}" ]; then
	MISTER_GCC_HOST_ARCH=x86_64
fi
GCC_PACKAGE_NAME=gcc-arm-$MISTER_GCC_VER-$MISTER_GCC_HOST_ARCH-arm-none-linux-gnueabihf
GCC_DIR=$MISTER_GCC_INSTALL_DIR/$GCC_PACKAGE_NAME

if [ ! -d $GCC_DIR ]; then
	echo "Downloading $GCC_PACKAGE_NAME..."
	GCC_TARBALL=$GCC_PACKAGE_NAME.tar.xz
	wget --no-check-certificate -c https://developer.arm.com/-/media/Files/downloads/gnu-a/$MISTER_GCC_VER/binrel/$GCC_TARBALL
	tar xvf $GCC_TARBALL
	rm $GCC_TARBALL
fi

echo "Setting environment variables..."
export CC=$GCC_DIR/bin/arm-none-gnueabihf-gcc
export PATH="$GCC_DIR/bin:$PATH"

echo "Done!"
