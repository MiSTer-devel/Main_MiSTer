#!/bin/bash

# NOTE: This script is meant to be run under Ubuntu Linux 22.04.2 LTS,
# including under WSL on Windows.

# Make script fail if any command failed, so we don't need
# to check the exit status of every command.
set -e
set -o pipefail

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
cd $SCRIPT_DIR

is_package_installed() {
    dpkg-query -Wf'${db:Status-abbrev}' "$1" 2>/dev/null | grep -q '^i' && dpkg-query -Wf'${Version}' "$1" 2>/dev/null | grep -qF "$2"
}

packages=(
	"make=4.3-4.1build1"
	"putty-tools=0.76-2"
)

for package in "${packages[@]}"; do
	IFS='=' read -ra package_parts <<< "$package"
	package_name=${package_parts[0]}
	package_version=${package_parts[1]}
	if ! is_package_installed $package_name $package_version; then
		sudo apt-get install -y $package
	fi
done

# This is the newest ARM GNU toolchain version that still uses glibc 2.31, which is what MiSTer's Linux OS still uses.
GCC_URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/10.2-2020.11/binrel/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf.tar.xz

# These newer toolchain versions use glibc >= 2.33, which MiSTer's Linux OS cannot (yet) support.
# So while you can build with these, the resultant binary will not run due to dependency version mismatch.
#GCC_URL=https://developer.arm.com/-/media/Files/downloads/gnu-a/10.3-2021.07/binrel/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf.tar.xz
#GCC_URL=https://developer.arm.com/-/media/Files/downloads/gnu/12.3.rel1/binrel/arm-gnu-toolchain-12.3.rel1-x86_64-arm-none-linux-gnueabihf.tar.xz

GCC_TARBALL=$( echo "$GCC_URL" | sed -E -e 's|^.*/([^/\?]+)(\?.*)?$|\1|g' )
GCC_DIR=gcc-arm

if [ ! -f $GCC_TARBALL ]; then
	echo "Downloading $GCC_TARBALL..."
	wget --no-check-certificate -c "$GCC_URL" -O /tmp/$GCC_TARBALL
	mv /tmp/$GCC_TARBALL ./$GCC_TARBALL
fi

GCC_PACKAGE_NAME=${GCC_TARBALL%.*.*}
GCC_SENTINEL_FILENAME=$GCC_PACKAGE_NAME.done

if [ ! -f $GCC_DIR/$GCC_SENTINEL_FILENAME ]; then
	if [ -d $GCC_DIR ]; then
		rm -rf $GCC_DIR
	fi

	mkdir $GCC_DIR
	PACKAGE_NAME_REGEX="${GCC_PACKAGE_NAME//./\\\.}"
	tar -x -v -f $GCC_TARBALL --show-transformed-names --transform=s/^$PACKAGE_NAME_REGEX/$GCC_DIR/g
	touch $GCC_DIR/$GCC_SENTINEL_FILENAME
fi
