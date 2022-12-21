#!/bin/bash

if [ "${BASH_SOURCE[0]}" -ef "$0" ]
then
    echo "This script should be sourced, not executed."
    exit 1
fi

echo "Setting up default toolchain..."

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
GCC_VER=10.2-2020.11
GCC_PACKAGE_NAME=gcc-arm-$GCC_VER-x86_64-arm-none-linux-gnueabihf
GCC_DIR=$SCRIPT_DIR/$GCC_PACKAGE_NAME

if [ ! -d $GCC_DIR ]; then
	echo "Downloading $GCC_PACKAGE_NAME..."
	GCC_TARBALL=$GCC_PACKAGE_NAME.tar.xz
	wget --no-check-certificate -c https://developer.arm.com/-/media/Files/downloads/gnu-a/$GCC_VER/binrel/$GCC_TARBALL
	tar xvf $GCC_TARBALL
	rm $GCC_TARBALL
fi

echo "Setting environment variables..."
export CC=$GCC_DIR/bin/arm-none-gnueabihf-gcc
export PATH="$GCC_DIR/bin:$PATH"

echo "Done!"

