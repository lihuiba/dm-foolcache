#!/bin/sh

kernel=/usr/src/linux-headers-`uname -r`/
echo "using kernel from '$kernel'"

make -C $kernel modules M=$PWD
#make -C ebtables/
