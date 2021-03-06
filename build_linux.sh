#!/bin/bash

sudo cp linux-patch/* linux-stable -r
cd linux-stable

if [ $# -lt 1 ]; then
	echo "Usage: $0 [arch]"
fi

case $1 in
	arm32)
		echo "Build arm32"
		export ARCH=arm
		export CROSS_COMPILE=arm-linux-gnueabi-
		#make distclean
		#make vexpress_defconfig
		#sudo cp ../linux-patch/include/generated/asm-offsets_.h include/generated/asm-offsets.h
    #mkdir -p _install_arm32/dev
		cd _install_arm32/dev/
		#sudo mknod console c 5 1
		cd ../..
		#make vexpress_defconfig
		make bzImage -j4
		#make dtbs
		#../collect-src -f "linux-stable" -o ../linux_arm32.list
		cd ..
		;;
	arm64)
		echo "Build arm64"
		export ARCH=arm64
		export CROSS_COMPILE=aarch64-linux-gnu-
		make defconfig
		make -j4
		make dtbs
		../collect-src -f "linux-stable" -o ../linux_arm64.list
		cd ..
		;;
esac

cat *.list | sort | uniq > linux_all.txt
mv linux_all.txt  linux_all.list
