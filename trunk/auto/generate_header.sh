#!/bin/bash
# genereate the library header file.

objs=$1

rm -f $objs/include/srs_librtmp.h &&
cp $objs/../src/libs/srs_librtmp.hpp $objs/include/srs_librtmp.h
echo "genereate srs-librtmp headers success"
