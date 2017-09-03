#!/bin/bash

# when options parsed, setup some variables, then build the depends.

# when arm specified, setup the cross build variables.
if [ $SRS_ARM_UBUNTU12 = YES ]; then
    __SrsArmCC="arm-linux-gnueabi-gcc";
    __SrsArmGCC="arm-linux-gnueabi-gcc";
    __SrsArmCXX="arm-linux-gnueabi-g++";
    __SrsArmAR="arm-linux-gnueabi-ar";
    __SrsArmLD="arm-linux-gnueabi-ld";
    __SrsArmRANDLIB="arm-linux-gnueabi-ranlib";
fi

if [ $SRS_MIPS_UBUNTU12 = YES ]; then
    __SrsArmCC="mipsel-openwrt-linux-gcc";
    __SrsArmGCC="mipsel-openwrt-linux-gcc";
    __SrsArmCXX="mipsel-openwrt-linux-g++";
    __SrsArmAR="mipsel-openwrt-linux-ar";
    __SrsArmLD="mipsel-openwrt-linux-ld";
    __SrsArmRANDLIB="mipsel-openwrt-linux-ranlib";
fi

# the arm-ubuntu12 options for make for depends
if [[ -z $SrsArmCC ]]; then SrsArmCC=$__SrsArmCC; fi
if [[ -z $SrsArmGCC ]]; then SrsArmGCC=$__SrsArmGCC; fi
if [[ -z $SrsArmCXX ]]; then SrsArmCXX=$__SrsArmCXX; fi
if [[ -z $SrsArmAR ]]; then SrsArmAR=$__SrsArmAR; fi
if [[ -z $SrsArmLD ]]; then SrsArmLD=$__SrsArmLD; fi
if [[ -z $SrsArmRANDLIB ]]; then SrsArmRANDLIB=$__SrsArmRANDLIB; fi
