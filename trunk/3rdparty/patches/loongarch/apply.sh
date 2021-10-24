#!/bin/bash

if [[ $OS_IS_LOONGARCH == YES ]]; then
    echo "Apply config patches for loongarch"

    rm -f config.guess config.sub &&
    cp ../../../3rdparty/patches/loongson/config.* .
    ret=$?; if [[ $ret -ne 0 ]]; then echo "apply failed, $ret"; exit $ret; fi

    echo "Apply config patches for OS_IS_LOONGARCH:${OS_IS_LOONGARCH} OK"
fi

