#!/bin/bash

# TODO: check gcc/g++
echo "check gcc/g++/gdb/make/openssl-devel"
echo "depends tools are ok"

#####################################################################################
# st-1.9
#####################################################################################
if [[ -f ${SRS_OBJS}/st-1.9/obj/libst.a && -f ${SRS_OBJS}/st-1.9/obj/libst.so ]]; then
    echo "st-1.9t is ok.";
else
    echo "build st-1.9t"; 
    (
        rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
        unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && make linux-debug &&
        cd .. && rm -f st && ln -sf st-1.9/obj st
    )
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "build st-1.9 failed, ret=$ret"; exit $ret; fi
if [ ! -f ${SRS_OBJS}/st-1.9/obj/libst.a ]; then echo "build st-1.9 failed."; exit -1; fi
if [ ! -f ${SRS_OBJS}/st-1.9/obj/libst.so ]; then echo "build st-1.9 failed."; exit -1; fi

#####################################################################################
# http-parser-2.1
#####################################################################################
if [[ -f ${SRS_OBJS}/http-parser-2.1/http_parser.h && -f ${SRS_OBJS}/http-parser-2.1/libhttp_parser.a ]]; then
    echo "http-parser-2.1 is ok.";
else
    echo "build http-parser-2.1";
    (
        rm -rf ${SRS_OBJS}/http-parser-2.1 && cd ${SRS_OBJS} && unzip -q ../3rdparty/http-parser-2.1.zip && 
        cd http-parser-2.1 && 
        sed -i "s/CPPFLAGS_FAST +=.*$/CPPFLAGS_FAST = \$\(CPPFLAGS_DEBUG\)/g" Makefile &&
        sed -i "s/CFLAGS_FAST =.*$/CFLAGS_FAST = \$\(CFLAGS_DEBUG\)/g" Makefile &&
        make package &&
        cd .. && rm -f hp && ln -sf http-parser-2.1 hp
    )
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "build http-parser-2.1 failed, ret=$ret"; exit $ret; fi
if [[ ! -f ${SRS_OBJS}/http-parser-2.1/http_parser.h ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
if [[ ! -f ${SRS_OBJS}/http-parser-2.1/libhttp_parser.a ]]; then echo "build http-parser-2.1 failed"; exit -1; fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
if [ $SRS_HLS = YES ]; then
    if [[ -f ${SRS_OBJS}/nginx/sbin/nginx ]]; then
        echo "nginx-1.5.7 is ok.";
    else
        echo "build nginx-1.5.7"; 
        (
            rm -rf ${SRS_OBJS}/nginx-1.5.7 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/nginx-1.5.7.zip && cd nginx-1.5.7 && 
            ./configure --prefix=`pwd`/_release && make && make install &&
            cd .. && ln -sf nginx-1.5.7/_release nginx
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build nginx-1.5.7 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/nginx/sbin/nginx ]; then echo "build nginx-1.5.7 failed."; exit -1; fi

    # use current user to config nginx,
    # srs will write ts/m3u8 file use current user,
    # nginx default use nobody, so cannot read the ts/m3u8 created by srs.
    cp ${SRS_OBJS}/nginx/conf/nginx.conf ${SRS_OBJS}/nginx/conf/nginx.conf.bk
    sed -i "s/^.user  nobody;/user `whoami`;/g" ${SRS_OBJS}/nginx/conf/nginx.conf
fi

if [ $SRS_HLS = YES ]; then
    echo "#define SRS_HLS" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HLS" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# openssl, for rtmp complex handshake
#####################################################################################
if [ $SRS_SSL = YES ]; then
    echo "#define SRS_SSL" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_SSL" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# live transcoding, ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
if [ $SRS_FFMPEG = YES ]; then
    if [[ -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
        echo "ffmpeg-2.1 is ok.";
    else
        echo "build ffmpeg-2.1"; 
        (
            cd ${SRS_OBJS} && pwd_dir=`pwd` && 
            rm -rf ffmepg.src && mkdir -p ffmpeg.src && cd ffmpeg.src &&
            rm -f build_ffmpeg.sh && ln -sf ../../auto/build_ffmpeg.sh && . build_ffmpeg.sh &&
            cd ${pwd_dir} && ln -sf ffmpeg.src/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build ffmpeg-2.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]; then echo "build ffmpeg-2.1 failed."; exit -1; fi
fi

if [ $SRS_FFMPEG = YES ]; then
    echo "#define SRS_FFMPEG" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_FFMPEG" >> $SRS_AUTO_HEADERS_H
fi
