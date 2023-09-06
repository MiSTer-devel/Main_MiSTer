#!/bin/bash

# NOTE: This script is meant to be run under Ubuntu Linux 22.04.2 LTS,
# including under WSL on Windows.

# Make script fail if any command failed, so we don't need
# to check the exit status of every command.
set -e
set -o pipefail

is_package_installed() {
    dpkg-query -Wf'${db:Status-abbrev}' "$1" 2>/dev/null | grep -q '^i' && dpkg-query -Wf'${Version}' "$1" 2>/dev/null | grep -qF "$2"
}

packages=(
	"make=4.3-4.1build1"
	"gcc-10-arm-linux-gnueabihf=10.5.0-1ubuntu1~22.04cross1"
	"g++-10-arm-linux-gnueabihf=10.5.0-1ubuntu1~22.04cross1"
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

# We only copy outside include dirs into the project folder
# for convenient reference by the VisualStudio solution.
include_dirs=(
	"/usr/arm-linux-gnueabihf/include"
	"/usr/lib/gcc-cross/arm-linux-gnueabihf/10/include"
)

for include_dir in "${include_dirs[@]}"; do
	relative_dir="ext-include/${include_dir#/}"
	if [ ! -d "$relative_dir" ]; then
		mkdir -p "${relative_dir%/*}"
		echo "Copying: $include_dir >> $PWD/$relative_dir"
		# NOTE: This can fail with errors of the following form:
		#
		#    cp: cannot create regular file 'ext-include/usr/arm-linux-gnueabihf/include/netfilter/xt_connmark.h': File exists
		#
		# This happens due to the destination filesystem (NTFS) not being
		# fully case-sensitive and thus incapable of undesrtanding that
		# xt_connmark.h and xt_CONNMARK.h are different files.  There's
		# nothing we can do about it except ignore the errors.
		cp -rd "$include_dir" "${relative_dir%/*}/" || true
	fi
done
