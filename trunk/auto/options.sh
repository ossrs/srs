#!/bin/bash

# variables, parent script must set it:

#####################################################################################
#####################################################################################
# parse user options, do this at first
#####################################################################################
#####################################################################################

#####################################################################################
# output variables
#####################################################################################
help=no

################################################################
# feature options
SRS_HDS=NO
SRS_SRT=NO
SRS_RTC=YES
SRS_GB28181=NO
SRS_CXX11=YES
SRS_CXX14=NO
SRS_NGINX=NO
SRS_LIBRTMP=NO
SRS_RESEARCH=NO
SRS_UTEST=NO
SRS_GPERF=NO # Performance test: tcmalloc
SRS_GPERF_MC=NO # Performance test: gperf memory check
SRS_GPERF_MD=NO # Performance test: gperf memory defence
SRS_GPERF_MP=NO # Performance test: gperf memory profile
SRS_GPERF_CP=NO # Performance test: gperf cpu profile
SRS_GPROF=NO # Performance test: gprof
# Always enable the bellow features.
SRS_STREAM_CASTER=YES
SRS_INGEST=YES
SRS_SSL=YES
SRS_SSL_1_0=NO
SRS_SSL_LOCAL=NO
SRS_HTTPS=YES
SRS_STAT=YES
SRS_TRANSCODE=YES
SRS_HTTP_CALLBACK=YES
SRS_HTTP_SERVER=YES
SRS_HTTP_API=YES
SRS_HTTP_CORE=YES
SRS_HLS=YES
SRS_DVR=YES
SRS_CHERRYPY=YES
# 
################################################################
# FFmpeg stub is the stub code in SRS for ingester or encoder.
SRS_FFMPEG_STUB=NO
# FFmpeg tool is the binary for FFmpeg tool, to exec ingest or transcode.
SRS_FFMPEG_TOOL=NO
# FFmpeg fit is the source code for RTC, to transcode audio or video in SRS.
SRS_FFMPEG_FIT=RESERVED
# arguments
SRS_PREFIX=/usr/local/srs
SRS_JOBS=1
SRS_STATIC=NO
# If enabled, link shared libraries for libst.so which uses MPL license.
SRS_SHARED_ST=NO
# If enabled, link shared libraries for libsrt.so which uses MPL license.
SRS_SHARED_SRT=NO
# whether enable the gcov
SRS_GCOV=NO
# whether enable the log verbose/info/trace level.
# always enable the warn/error level.
SRS_LOG_VERBOSE=NO
SRS_LOG_INFO=NO
SRS_LOG_TRACE=YES
#
################################################################
# experts
# donot compile ssl, use system ssl(-lssl) if required.
# TODO: Use pkg-config to get the openssl path.
SRS_USE_SYS_SSL=NO
# export the srs-librtmp to specified project, NO to disable it.
SRS_EXPORT_LIBRTMP_PROJECT=NO
# export the srs-librtmp to a single .h and .c, NO to disable it.
SRS_EXPORT_LIBRTMP_SINGLE=NO
# valgrind
SRS_VALGRIND=NO
# Set the object files tag name.
SRS_BUILD_TAG=
# Whether do "make clean" when configure.
SRS_CLEAN=YES
# Whether enable RTC simulate API.
SRS_SIMULATOR=NO
#
################################################################
# presets
# for x86/x64 pc/servers
SRS_X86_X64=NO
# for osx system
SRS_OSX=NO
# dev, open all features for dev, no gperf/prof/arm.
SRS_DEV=NO
# dev, open main server feature for dev, no utest/research/librtmp
SRS_FAST_DEV=NO
# demo, for the demo of srs, @see: https://github.com/ossrs/srs/wiki/v1_CN_SampleDemo
SRS_DEMO=NO
# raspberry-pi, open hls/ssl/static
SRS_PI=NO
# cubieboard, donot open ffmpeg/nginx.
SRS_CUBIE=NO
# the most fast compile, nothing, only support vp6 RTMP.
SRS_FAST=NO
# only support RTMP with ssl.
SRS_PURE_RTMP=NO
# the most fast compile, nothing, only support vp6 RTMP.
SRS_DISABLE_ALL=NO
# all features is on
SRS_ENABLE_ALL=NO
#
#####################################################################################
# Toolchain crossbuild for ARM or MIPS.
SRS_CROSS_BUILD=NO
SRS_TOOL_CC=gcc
SRS_TOOL_CXX=g++
SRS_TOOL_AR=ar
SRS_TOOL_LD=ld
SRS_TOOL_RANDLIB=randlib
SRS_EXTRA_FLAGS=
#
#####################################################################################
# Performance optimize.
SRS_NASM=YES
SRS_SRTP_ASM=YES
SRS_SENDMMSG=NO
SRS_DEBUG=NO
SRS_DEBUG_STATS=NO

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Presets:
  --x86-64, --x86-x64       For x86/x64 cpu, common pc and servers. Default: $(value2switch $SRS_X86_X64)
  --arm                     Enable crossbuild for ARM, should also set bellow toolchain options. Default: $(value2switch $SRS_CROSS_BUILD)
  --osx                     Enable build for OSX/Darwin AppleOS. Default: $(value2switch $SRS_OSX)

Features:
  -h, --help                Print this message and exit 0.

  --https=on|off            Whether enable HTTPS client and server. Default: $(value2switch $SRS_HTTPS)
  --hds=on|off              Whether build the hds streaming, mux RTMP to F4M/F4V files. Default: $(value2switch $SRS_HDS)
  --cherrypy=on|off         Whether install CherryPy for demo api-server. Default: $(value2switch $SRS_CHERRYPY)
  --utest=on|off            Whether build the utest. Default: $(value2switch $SRS_UTEST)
  --srt=on|off              Whether build the SRT. Default: $(value2switch $SRS_SRT)
  --rtc=on|off              Whether build the WebRTC. Default: $(value2switch $SRS_RTC)
  --gb28181=on|off          Whether build the GB28181. Default: $(value2switch $SRS_GB28181)
  --cxx11=on|off            Whether enable the C++11. Default: $(value2switch $SRS_CXX11)
  --cxx14=on|off            Whether enable the C++14. Default: $(value2switch $SRS_CXX14)
  --ffmpeg-fit=on|off       Whether enable the FFmpeg fit(source code). Default: $(value2switch $SRS_FFMPEG_FIT)

  --prefix=<path>           The absolute installation path. Default: $SRS_PREFIX
  --gcov=on|off             Whether enable the GCOV compiler options. Default: $(value2switch $SRS_GCOV)
  --debug=on|off            Whether enable the debug code, may hurt performance. Default: $(value2switch $SRS_DEBUG)
  --debug-stats=on|off      Whether enable the debug stats, may hurt performance. Default: $(value2switch $SRS_DEBUG_STATS)
  --jobs[=N]                Allow N jobs at once; infinite jobs with no arg. Default: $SRS_JOBS
  --log-verbose=on|off      Whether enable the log verbose level. Default: $(value2switch $SRS_LOG_VERBOSE)
  --log-info=on|off         Whether enable the log info level. Default: $(value2switch $SRS_LOG_INFO)
  --log-trace=on|off        Whether enable the log trace level. Default: $(value2switch $SRS_LOG_TRACE)

Performance:                @see https://blog.csdn.net/win_lin/article/details/53503869
  --valgrind=on|off         Whether build valgrind for memory check. Default: $(value2switch $SRS_VALGRIND)
  --gperf=on|off            Whether build SRS with gperf tools(no gmd/gmc/gmp/gcp, with tcmalloc only). Default: $(value2switch $SRS_GPERF)
  --gmc=on|off              Whether build memory check with gperf tools. Default: $(value2switch $SRS_GPERF_MC)
  --gmd=on|off              Whether build memory defense(corrupt memory) with gperf tools. Default: $(value2switch $SRS_GPERF_MD)
  --gmp=on|off              Whether build memory profile with gperf tools. Default: $(value2switch $SRS_GPERF_MP)
  --gcp=on|off              Whether build cpu profile with gperf tools. Default: $(value2switch $SRS_GPERF_CP)
  --gprof=on|off            Whether build SRS with gprof(GNU profile tool). Default: $(value2switch $SRS_GPROF)

  --nasm=on|off             Whether build FFMPEG for RTC with nasm. Default: $(value2switch $SRS_NASM)
  --srtp-nasm=on|off        Whether build SRTP with ASM(openssl-asm), requires RTC and openssl-1.0.*. Default: $(value2switch $SRS_SRTP_ASM)
  --sendmmsg=on|off         Whether enable UDP sendmmsg. Default: $(value2switch $SRS_SENDMMSG). @see http://man7.org/linux/man-pages/man2/sendmmsg.2.html

Toolchain options:          @see https://github.com/ossrs/srs/issues/1547#issuecomment-576078411
  --static=on|off           Whether add '-static' to link options. Default: $(value2switch $SRS_STATIC)
  --cc=<CC>                 Use c compiler CC. Default: $SRS_TOOL_CC
  --cxx=<CXX>               Use c++ compiler CXX. Default: $SRS_TOOL_CXX
  --ar=<AR>                 Use archive tool AR. Default: $SRS_TOOL_CXX
  --ld=<LD>                 Use linker tool LD. Default: $SRS_TOOL_CXX
  --randlib=<RANDLIB>       Use randlib tool RANDLIB. Default: $SRS_TOOL_CXX
  --extra-flags=<EFLAGS>    Set EFLAGS as CFLAGS and CXXFLAGS. Also passed to ST as EXTRA_CFLAGS.

Experts:
  --sys-ssl=on|off          Do not compile ssl, use system ssl(-lssl) if required. Default: $(value2switch $SRS_USE_SYS_SSL)
  --ssl-1-0=on|off          Whether use openssl-1.0.*. Default: $(value2switch $SRS_SSL_1_0)
  --ssl-local=on|off        Whether use local openssl, not system even exists. Default: $(value2switch $SRS_SSL_LOCAL)
  --use-shared-st           Use link shared libraries for ST which uses MPL license. Default: $(value2switch $SRS_SHARED_ST)
  --use-shared-srt          Use link shared libraries for SRT which uses MPL license. Default: $(value2switch $SRS_SHARED_SRT)
  --clean=on|off            Whether do 'make clean' when configure. Default: $(value2switch $SRS_CLEAN)
  --simulator=on|off        Whether enable RTC network simulator. Default: $(value2switch $SRS_SIMULATOR)
  --build-tag=<TAG>         Set the build object directory suffix.

Workflow:
  1. Apply "Presets". if not specified, use default preset.
  2. Apply "Features", "Performance" and others. user specified option will override the preset.
  3. Check configs and generate Makefile.

Remark:
  1. For performance, read https://blog.csdn.net/win_lin/article/details/53503869

END
}

function parse_user_option() {
    case "$option" in
        -h)                             help=yes                    ;;
        --help)                         help=yes                    ;;
        
        --jobs)                         SRS_JOBS=${value}           ;;
        --prefix)                       SRS_PREFIX=${value}         ;;

        --static)                       if [[ $value == off ]]; then SRS_STATIC=NO; else SRS_STATIC=YES; fi    ;;
        --cc)                           SRS_TOOL_CC=${value}        ;;
        --cxx)                          SRS_TOOL_CXX=${value}       ;;
        --ar)                           SRS_TOOL_AR=${value}        ;;
        --ld)                           SRS_TOOL_LD=${value}        ;;
        --randlib)                      SRS_TOOL_RANDLIB=${value}   ;;
        --extra-flags)                  SRS_EXTRA_FLAGS=${value}    ;;
        --build-tag)                    SRS_BUILD_TAG=${value}      ;;

        --x86-x64)                      SRS_X86_X64=YES             ;;
        --x86-64)                       SRS_X86_X64=YES             ;;
        --osx)                          SRS_OSX=YES                 ;;

        --sendmmsg)                     if [[ $value == off ]]; then SRS_SENDMMSG=NO; else SRS_SENDMMSG=YES; fi    ;;

        --without-srtp-nasm)            SRS_SRTP_ASM=NO             ;;
        --with-srtp-nasm)               SRS_SRTP_ASM=YES            ;;
        --srtp-nasm)                    if [[ $value == off ]]; then SRS_SRTP_ASM=NO; else SRS_SRTP_ASM=YES; fi    ;;

        --without-nasm)                 SRS_NASM=NO                 ;;
        --with-nasm)                    SRS_NASM=YES                ;;
        --nasm)                         if [[ $value == off ]]; then SRS_NASM=NO; else SRS_NASM=YES; fi    ;;

        --with-ssl)                     SRS_SSL=YES                 ;;
        --ssl)                          if [[ $value == off ]]; then SRS_SSL=NO; else SRS_SSL=YES; fi    ;;
        --https)                        if [[ $value == off ]]; then SRS_HTTPS=NO; else SRS_HTTPS=YES; fi ;;
        --ssl-1-0)                      if [[ $value == off ]]; then SRS_SSL_1_0=NO; else SRS_SSL_1_0=YES; fi ;;
        --ssl-local)                    if [[ $value == off ]]; then SRS_SSL_LOCAL=NO; else SRS_SSL_LOCAL=YES; fi ;;

        --with-hds)                     SRS_HDS=YES                 ;;
        --without-hds)                  SRS_HDS=NO                  ;;
        --hds)                          if [[ $value == off ]]; then SRS_HDS=NO; else SRS_HDS=YES; fi    ;;

        --with-transcode)               SRS_TRANSCODE=YES           ;;
        --without-transcode)            echo "ignore option \"$option\"" ;;
        --transcode)                    if [[ $value == off ]]; then SRS_TRANSCODE=NO; else SRS_TRANSCODE=YES; fi    ;;

        --with-ingest)                  SRS_INGEST=YES              ;;
        --without-ingest)               echo "ignore option \"$option\"" ;;
        --ingest)                       if [[ $value == off ]]; then SRS_INGEST=NO; else SRS_INGEST=YES; fi    ;;

        --with-stat)                    SRS_STAT=YES                ;;
        --without-stat)                 echo "ignore option \"$option\"" ;;
        --stat)                         if [[ $value == off ]]; then SRS_STAT=NO; else SRS_STAT=YES; fi    ;;

        --with-stream-caster)           SRS_STREAM_CASTER=YES       ;;
        --without-stream-caster)        echo "ignore option \"$option\"" ;;
        --stream-caster)                if [[ $value == off ]]; then SRS_STREAM_CASTER=NO; else SRS_STREAM_CASTER=YES; fi    ;;

        --with-research)                SRS_RESEARCH=YES            ;;
        --without-research)             SRS_RESEARCH=NO             ;;
        --research)                     if [[ $value == off ]]; then SRS_RESEARCH=NO; else SRS_RESEARCH=YES; fi    ;;

        --with-utest)                   SRS_UTEST=YES               ;;
        --without-utest)                SRS_UTEST=NO                ;;
        --utest)                        if [[ $value == off ]]; then SRS_UTEST=NO; else SRS_UTEST=YES; fi    ;;
        --cherrypy)                     if [[ $value == off ]]; then SRS_CHERRYPY=NO; else SRS_CHERRYPY=YES; fi    ;;
        --gcov)                         if [[ $value == off ]]; then SRS_GCOV=NO; else SRS_GCOV=YES; fi    ;;

        --with-srt)                     SRS_SRT=YES                 ;;
        --without-srt)                  SRS_SRT=NO                  ;;
        --srt)                          if [[ $value == off ]]; then SRS_SRT=NO; else SRS_SRT=YES; fi    ;;

        --with-rtc)                     SRS_RTC=YES                 ;;
        --without-rtc)                  SRS_RTC=NO                  ;;
        --rtc)                          if [[ $value == off ]]; then SRS_RTC=NO; else SRS_RTC=YES; fi    ;;
        --simulator)                    if [[ $value == off ]]; then SRS_SIMULATOR=NO; else SRS_SIMULATOR=YES; fi    ;;
        --ffmpeg-fit)                   if [[ $value == off ]]; then SRS_FFMPEG_FIT=NO; else SRS_FFMPEG_FIT=YES; fi    ;;

        --with-gb28181)                 SRS_GB28181=YES             ;;
        --without-gb28181)              SRS_GB28181=NO              ;;
        --gb28181)                      if [[ $value == off ]]; then SRS_GB28181=NO; else SRS_GB28181=YES; fi    ;;

        --cxx11)                        if [[ $value == off ]]; then SRS_CXX11=NO; else SRS_CXX11=YES; fi    ;;
        --cxx14)                        if [[ $value == off ]]; then SRS_CXX14=NO; else SRS_CXX14=YES; fi    ;;

        --with-clean)                   SRS_CLEAN=YES               ;;
        --without-clean)                SRS_CLEAN=NO                ;;
        --clean)                        if [[ $value == off ]]; then SRS_CLEAN=NO; else SRS_CLEAN=YES; fi    ;;

        --with-gperf)                   SRS_GPERF=YES               ;;
        --without-gperf)                SRS_GPERF=NO                ;;
        --gperf)                        if [[ $value == off ]]; then SRS_GPERF=NO; else SRS_GPERF=YES; fi    ;;

        --with-gmc)                     SRS_GPERF_MC=YES            ;;
        --without-gmc)                  SRS_GPERF_MC=NO             ;;
        --gmc)                          if [[ $value == off ]]; then SRS_GPERF_MC=NO; else SRS_GPERF_MC=YES; fi    ;;

        --with-gmd)                     SRS_GPERF_MD=YES            ;;
        --without-gmd)                  SRS_GPERF_MD=NO             ;;
        --gmd)                          if [[ $value == off ]]; then SRS_GPERF_MD=NO; else SRS_GPERF_MD=YES; fi    ;;

        --with-gmp)                     SRS_GPERF_MP=YES            ;;
        --without-gmp)                  SRS_GPERF_MP=NO             ;;
        --gmp)                          if [[ $value == off ]]; then SRS_GPERF_MP=NO; else SRS_GPERF_MP=YES; fi    ;;

        --with-gcp)                     SRS_GPERF_CP=YES            ;;
        --without-gcp)                  SRS_GPERF_CP=NO             ;;
        --gcp)                          if [[ $value == off ]]; then SRS_GPERF_CP=NO; else SRS_GPERF_CP=YES; fi    ;;

        --with-gprof)                   SRS_GPROF=YES               ;;
        --without-gprof)                SRS_GPROF=NO                ;;
        --gprof)                        if [[ $value == off ]]; then SRS_GPROF=NO; else SRS_GPROF=YES; fi    ;;

        --use-sys-ssl)                  SRS_USE_SYS_SSL=YES         ;;
        --without-ssl)                  echo "ignore option \"$option\"" ;;
        --sys-ssl)                      if [[ $value == off ]]; then SRS_USE_SYS_SSL=NO; else SRS_USE_SYS_SSL=YES; fi    ;;

        --use-shared-st)                SRS_SHARED_ST=YES           ;;
        --shared-st)                    if [[ $value == off ]]; then SRS_SHARED_ST=NO; else SRS_SHARED_ST=YES; fi    ;;

        --use-shared-srt)               SRS_SHARED_SRT=YES          ;;
        --shared-srt)                   if [[ $value == off ]]; then SRS_SHARED_SRT=NO; else SRS_SHARED_SRT=YES; fi    ;;

        --with-valgrind)                SRS_VALGRIND=YES            ;;
        --without-valgrind)             SRS_VALGRIND=NO             ;;
        --valgrind)                     if [[ $value == off ]]; then SRS_VALGRIND=NO; else SRS_VALGRIND=YES; fi    ;;

        --with-http-callback)           echo "ignore option \"$option\"" ;;
        --without-http-callback)        echo "ignore option \"$option\"" ;;
        --http-callback)                echo "ignore option \"$option\"" ;;

        --with-http-api)                echo "ignore option \"$option\"" ;;
        --without-http-api)             echo "ignore option \"$option\"" ;;
        --http-api)                     echo "ignore option \"$option\"" ;;

        --with-http-server)             echo "ignore option \"$option\"" ;;
        --without-http-server)          echo "ignore option \"$option\"" ;;
        --http-server)                  echo "ignore option \"$option\"" ;;

        --with-hls)                     echo "ignore option \"$option\"" ;;
        --without-hls)                  echo "ignore option \"$option\"" ;;
        --hls)                          echo "ignore option \"$option\"" ;;

        --with-dvr)                     echo "ignore option \"$option\"" ;;
        --without-dvr)                  echo "ignore option \"$option\"" ;;
        --dvr)                          echo "ignore option \"$option\"" ;;

        --log-verbose)                  if [[ $value == off ]]; then SRS_LOG_VERBOSE=NO; else SRS_LOG_VERBOSE=YES; fi    ;;
        --log-info)                     if [[ $value == off ]]; then SRS_LOG_INFO=NO; else SRS_LOG_INFO=YES; fi    ;;
        --log-trace)                    if [[ $value == off ]]; then SRS_LOG_TRACE=NO; else SRS_LOG_TRACE=YES; fi    ;;
        --debug)                        if [[ $value == off ]]; then SRS_DEBUG=NO; else SRS_DEBUG=YES; fi    ;;
        --debug-stats)                  if [[ $value == off ]]; then SRS_DEBUG_STATS=NO; else SRS_DEBUG_STATS=YES; fi    ;;

        # Deprecated, might be removed in future.
        --arm)                          SRS_CROSS_BUILD=YES         ;;
        --mips)                         SRS_CROSS_BUILD=YES         ;;
        --pi)                           SRS_PI=YES                  ;;
        --cubie)                        SRS_CUBIE=YES               ;;
        --dev)                          SRS_DEV=YES                 ;;
        --fast-dev)                     SRS_FAST_DEV=YES            ;;
        --demo)                         SRS_DEMO=YES                ;;
        --fast)                         SRS_FAST=YES                ;;
        --disable-all)                  SRS_DISABLE_ALL=YES         ;;
        --pure-rtmp)                    SRS_PURE_RTMP=YES           ;;
        --full)                         SRS_ENABLE_ALL=YES          ;;
        --export-librtmp-project)       SRS_EXPORT_LIBRTMP_PROJECT=${value}     ;;
        --export-librtmp-single)        SRS_EXPORT_LIBRTMP_SINGLE=${value}      ;;
        --with-nginx)                   SRS_NGINX=YES               ;;
        --without-nginx)                SRS_NGINX=NO                ;;
        --nginx)                        if [[ $value == off ]]; then SRS_NGINX=NO; else SRS_NGINX=YES; fi    ;;
        --with-ffmpeg)                  SRS_FFMPEG_TOOL=YES         ;;
        --without-ffmpeg)               SRS_FFMPEG_TOOL=NO          ;;
        --ffmpeg-tool)                  if [[ $value == off ]]; then SRS_FFMPEG_TOOL=NO; else SRS_FFMPEG_TOOL=YES; fi    ;;
        --with-librtmp)                 SRS_LIBRTMP=YES             ;;
        --without-librtmp)              SRS_LIBRTMP=NO              ;;
        --librtmp)                      if [[ $value == off ]]; then SRS_LIBRTMP=NO; else SRS_LIBRTMP=YES; fi    ;;
        --with-arm-ubuntu12)            SRS_CROSS_BUILD=YES         ;;
        --without-arm-ubuntu12)         SRS_CROSS_BUILD=NO          ;;
        --arm-ubuntu12)                 if [[ $value == off ]]; then SRS_CROSS_BUILD=NO; else SRS_CROSS_BUILD=YES; fi    ;;
        --with-mips-ubuntu12)           SRS_CROSS_BUILD=YES         ;;
        --without-mips-ubuntu12)        SRS_CROSS_BUILD=NO          ;;
        --mips-ubuntu12)                if [[ $value == off ]]; then SRS_CROSS_BUILD=NO; else SRS_CROSS_BUILD=YES; fi    ;;

        *)
            echo "$0: error: invalid option \"$option\""
            exit 1
        ;;
    esac
}

function parse_user_option_to_value_and_option() {
    case "$option" in
        -*=*) 
            value=`echo "$option" | sed -e 's|[-_a-zA-Z0-9/]*=||'`
            option=`echo "$option" | sed -e 's|=[-_a-zA-Z0-9/. +]*||'`
        ;;
           *) value="" ;;
    esac
}

function value2switch() {
    if [[ $1 == YES ]]; then
      echo on;
    elif [[ $1 == NO ]]; then
      echo off;
    else
      echo undefined;
    fi
}

function switch2value() {
    if [[ $1 == on ]]; then
      echo YES;
    elif [[ $1 == off ]]; then
      echo NO;
    else
      echo undefined;
    fi
}

#####################################################################################
# parse preset options
#####################################################################################
opt=

for option
do
    opt="$opt `echo $option | sed -e \"s/\(--[^=]*=\)\(.* .*\)/\1'\2'/\"`"
    parse_user_option_to_value_and_option
    parse_user_option
done

if [ $help = yes ]; then
    show_help
    exit 0
fi

function apply_detail_options() {
    # set default preset if not specifies
    if [[ $SRS_X86_X64 == NO && $SRS_OSX == NO && $SRS_CROSS_BUILD == NO ]]; then
        SRS_X86_X64=YES; opt="--x86-x64 $opt";
    fi

    # Enable c++11 for SRT.
    if [[ $SRS_SRT == YES ]]; then
        SRS_CXX11=YES
    fi

    # Enable FFmpeg fit for RTC to trancode audio from AAC to OPUS, if user has't disabled it.
    if [[ $SRS_RTC == YES && $SRS_FFMPEG_FIT == RESERVED ]]; then
        SRS_FFMPEG_FIT=YES
    fi

    # if transcode/ingest specified, requires the ffmpeg stub classes.
    SRS_FFMPEG_STUB=NO
    if [ $SRS_TRANSCODE = YES ]; then SRS_FFMPEG_STUB=YES; fi
    if [ $SRS_INGEST = YES ]; then SRS_FFMPEG_STUB=YES; fi

    # Always enable HTTP utilies.
    if [ $SRS_HTTP_CORE = NO ]; then SRS_HTTP_CORE=YES; echo -e "${YELLOW}[WARN] Always enable HTTP utilies.${BLACK}"; fi
    if [ $SRS_STREAM_CASTER = NO ]; then SRS_STREAM_CASTER=YES; echo -e "${YELLOW}[WARN] Always enable StreamCaster.${BLACK}"; fi
    if [ $SRS_INGEST = NO ]; then SRS_INGEST=YES; echo -e "${YELLOW}[WARN] Always enable Ingest.${BLACK}"; fi
    if [ $SRS_SSL = NO ]; then SRS_SSL=YES; echo -e "${YELLOW}[WARN] Always enable SSL.${BLACK}"; fi
    if [ $SRS_STAT = NO ]; then SRS_STAT=YES; echo -e "${YELLOW}[WARN] Always enable Statistic.${BLACK}"; fi
    if [ $SRS_TRANSCODE = NO ]; then SRS_TRANSCODE=YES; echo -e "${YELLOW}[WARN] Always enable Transcode.${BLACK}"; fi
    if [ $SRS_HTTP_CALLBACK = NO ]; then SRS_HTTP_CALLBACK=YES; echo -e "${YELLOW}[WARN] Always enable HTTP callback.${BLACK}"; fi
    if [ $SRS_HTTP_SERVER = NO ]; then SRS_HTTP_SERVER=YES; echo -e "${YELLOW}[WARN] Always enable HTTP server.${BLACK}"; fi
    if [ $SRS_HTTP_API = NO ]; then SRS_HTTP_API=YES; echo -e "${YELLOW}[WARN] Always enable HTTP API.${BLACK}"; fi
    if [ $SRS_HLS = NO ]; then SRS_HLS=YES; echo -e "${YELLOW}[WARN] Always enable HLS.${BLACK}"; fi
    if [ $SRS_DVR = NO ]; then SRS_DVR=YES; echo -e "${YELLOW}[WARN] Always enable DVR.${BLACK}"; fi

    # parse the jobs for make
    if [[ "" -eq SRS_JOBS ]]; then 
        export SRS_JOBS="--jobs=1" 
    else
        export SRS_JOBS="--jobs=${SRS_JOBS}"
    fi
    
    # if specified export single file, export project first.
    if [ $SRS_EXPORT_LIBRTMP_SINGLE != NO ]; then
        echo "Warning: Ingore --export-librtmp-single"
        SRS_EXPORT_LIBRTMP_SINGLE=NO
    fi

    # disable almost all features for export srs-librtmp.
    if [ $SRS_EXPORT_LIBRTMP_PROJECT != NO ]; then
        echo "Warning: Ingore --export-librtmp-project"
        SRS_EXPORT_LIBRTMP_PROJECT=NO
    fi

    if [[ $SRS_LIBRTMP != NO ]]; then
        echo "Warning: Ingore --librtmp"
        SRS_LIBRTMP=NO
    fi

    if [[ $SRS_RESEARCH != NO ]]; then
        echo "Warning: Ingore --research"
        SRS_RESEARCH=NO
    fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_RTC == NO ]]; then
        echo "Disable SRTP-ASM, because RTC is disabled."
        SRS_SRTP_ASM=NO
    fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_NASM == NO ]]; then
        echo "Disable SRTP-ASM, because NASM is disabled."
        SRS_SRTP_ASM=NO
    fi

    # Which openssl we choose, openssl-1.0.* for SRTP with ASM, others we use openssl-1.1.*
    if [[ $SRS_SRTP_ASM == YES && $SRS_SSL_1_0 == NO ]]; then
        echo "Use openssl-1.0 for SRTP-ASM."
        SRS_SSL_1_0=YES
    fi

    if [[ $SRS_OSX == YES && $SRS_SENDMMSG == YES ]]; then
        echo "Disable sendmmsg for OSX"
        SRS_SENDMMSG=NO
    fi
}
apply_detail_options

function regenerate_options() {
    # save all config options to macro to write to auto headers file
    SRS_AUTO_USER_CONFIGURE=`echo $opt`
    # regenerate the options for default values.
    SRS_AUTO_CONFIGURE="--prefix=${SRS_PREFIX}"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --hls=$(value2switch $SRS_HLS)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --hds=$(value2switch $SRS_HDS)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --dvr=$(value2switch $SRS_DVR)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ssl=$(value2switch $SRS_SSL)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --https=$(value2switch $SRS_HTTPS)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ssl-1-0=$(value2switch $SRS_SSL_1_0)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ssl-local=$(value2switch $SRS_SSL_LOCAL)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --sys-ssl=$(value2switch $SRS_USE_SYS_SSL)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --transcode=$(value2switch $SRS_TRANSCODE)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ingest=$(value2switch $SRS_INGEST)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --stat=$(value2switch $SRS_STAT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --http-callback=$(value2switch $SRS_HTTP_CALLBACK)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --http-server=$(value2switch $SRS_HTTP_SERVER)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --stream-caster=$(value2switch $SRS_STREAM_CASTER)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --http-api=$(value2switch $SRS_HTTP_API)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --utest=$(value2switch $SRS_UTEST)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cherrypy=$(value2switch $SRS_CHERRYPY)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --srt=$(value2switch $SRS_SRT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --rtc=$(value2switch $SRS_RTC)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --simulator=$(value2switch $SRS_SIMULATOR)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gb28181=$(value2switch $SRS_GB28181)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx11=$(value2switch $SRS_CXX11)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx14=$(value2switch $SRS_CXX14)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ffmpeg-fit=$(value2switch $SRS_FFMPEG_FIT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --nasm=$(value2switch $SRS_NASM)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --srtp-nasm=$(value2switch $SRS_SRTP_ASM)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --sendmmsg=$(value2switch $SRS_SENDMMSG)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --clean=$(value2switch $SRS_CLEAN)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gperf=$(value2switch $SRS_GPERF)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmc=$(value2switch $SRS_GPERF_MC)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmd=$(value2switch $SRS_GPERF_MD)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmp=$(value2switch $SRS_GPERF_MP)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gcp=$(value2switch $SRS_GPERF_CP)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gprof=$(value2switch $SRS_GPROF)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --static=$(value2switch $SRS_STATIC)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --use-shared-st=$(value2switch $SRS_SHARED_ST)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --use-shared-srt=$(value2switch $SRS_SHARED_SRT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-verbose=$(value2switch $SRS_LOG_VERBOSE)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-info=$(value2switch $SRS_LOG_INFO)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-trace=$(value2switch $SRS_LOG_TRACE)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gcov=$(value2switch $SRS_GCOV)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --debug=$(value2switch $SRS_DEBUG)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --debug-stats=$(value2switch $SRS_DEBUG_STATS)"
    if [[ $SRS_EXTRA_FLAGS != '' ]]; then   SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --extra-flags=\\\"$SRS_EXTRA_FLAGS\\\""; fi
    if [[ $SRS_BUILD_TAG != '' ]]; then     SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --build-tag=\\\"$SRS_BUILD_TAG\\\""; fi
    if [[ $SRS_TOOL_CC != '' ]]; then       SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cc=$SRS_TOOL_CC"; fi
    if [[ $SRS_TOOL_CXX != '' ]]; then      SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx=$SRS_TOOL_CXX"; fi
    if [[ $SRS_TOOL_AR != '' ]]; then       SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ar=$SRS_TOOL_AR"; fi
    if [[ $SRS_TOOL_LD != '' ]]; then       SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ld=$SRS_TOOL_LD"; fi
    if [[ $SRS_TOOL_RANDLIB != '' ]]; then  SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --randlib=$SRS_TOOL_RANDLIB"; fi
    echo "User config: $SRS_AUTO_USER_CONFIGURE"
    echo "Detail config: ${SRS_AUTO_CONFIGURE}"
}
regenerate_options

#####################################################################################
# check user options
#####################################################################################
function check_option_conflicts() {
    if [[ $SRS_TOOL_CC == '' ||  $SRS_TOOL_CXX == '' ||  $SRS_TOOL_AR == '' ||  $SRS_TOOL_LD == '' ||  $SRS_TOOL_RANDLIB == '' ]]; then
        echo "No crossbuild tools, cc: $SRS_TOOL_CC, cxx: $SRS_TOOL_CXX, ar: $SRS_TOOL_AR, ld: $SRS_TOOL_LD, randlib: $SRS_TOOL_RANDLIB"; exit -1
    fi

    if [[ $SRS_CROSS_BUILD == YES && ($SRS_TOOL_CC == 'gcc' || $SRS_TOOL_CXX == 'g++' || $SRS_TOOL_AR == 'ar') ]]; then
        echo "Warning: For crossbuild, must not use default toolchain, cc: $SRS_TOOL_CC, cxx: $SRS_TOOL_CXX, ar: $SRS_TOOL_AR"
        SRS_CROSS_BUILD=NO
    fi

    if [[ $SRS_NGINX == YES ]]; then
        echo "Warning: Don't support building NGINX, please use docker https://github.com/ossrs/srs-docker"
        SRS_NGINX=NO
    fi

    # TODO: FIXME: check more os.

    __check_ok=YES
    # check conflict
    if [ $SRS_GPERF = NO ]; then
        if [ $SRS_GPERF_MC = YES ]; then echo "gperf-mc depends on gperf, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_MD = YES ]; then echo "gperf-md depends on gperf, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_MP = YES ]; then echo "gperf-mp depends on gperf, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_CP = YES ]; then echo "gperf-cp depends on gperf, see: ./configure --help"; __check_ok=NO; fi
    fi
    if [[ $SRS_GPERF_MC = YES && $SRS_GPERF_MP = YES ]]; then
        echo "gperf-mc not compatible with gperf-mp, see: ./configure --help";
        echo "@see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html";
        echo "Note that since the heap-checker uses the heap-profiling framework internally, it is not possible to run both the heap-checker and heap profiler at the same time";
        __check_ok=NO
    fi
    # generate the group option: SRS_GPERF
    __gperf_slow=NO
    if [ $SRS_GPERF_MC = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
    if [ $SRS_GPERF_MD = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
    if [ $SRS_GPERF_MP = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
    if [ $SRS_GPERF_CP = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
    if [ $__gperf_slow = YES ]; then if [ $SRS_GPROF = YES ]; then 
        echo "gmc/gmp/gcp not compatible with gprof, see: ./configure --help"; __check_ok=NO; 
    fi fi

    # check variable neccessary
    if [ $SRS_HDS = RESERVED ]; then echo "you must specifies the hds, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_SSL = RESERVED ]; then echo "you must specifies the ssl, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_STREAM_CASTER = RESERVED ]; then echo "you must specifies the stream-caster, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_UTEST = RESERVED ]; then echo "you must specifies the utest, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF = RESERVED ]; then echo "you must specifies the gperf, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MC = RESERVED ]; then echo "you must specifies the gperf-mc, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MD = RESERVED ]; then echo "you must specifies the gperf-md, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MP = RESERVED ]; then echo "you must specifies the gperf-mp, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_CP = RESERVED ]; then echo "you must specifies the gperf-cp, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPROF = RESERVED ]; then echo "you must specifies the gprof, see: ./configure --help"; __check_ok=NO; fi
    if [[ -z $SRS_PREFIX ]]; then echo "you must specifies the prefix, see: ./configure --prefix"; __check_ok=NO; fi
    if [ $__check_ok = NO ]; then
        exit 1;
    fi
}
check_option_conflicts
