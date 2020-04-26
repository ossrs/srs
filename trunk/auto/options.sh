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
SRS_NGINX=NO
SRS_FFMPEG_TOOL=NO
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
SRS_STAT=YES
SRS_TRANSCODE=YES
SRS_HTTP_CALLBACK=YES
SRS_HTTP_SERVER=YES
SRS_HTTP_API=YES
SRS_HTTP_CORE=YES
SRS_HLS=YES
SRS_DVR=YES
# 
################################################################
# libraries
SRS_FFMPEG_STUB=NO
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
SRS_LOG_TRACE=NO
#
################################################################
# experts
# donot compile ssl, use system ssl(-lssl) if required.
# TODO: Use pkg-config to get the openssl path.
SRS_USE_SYS_SSL=NO
# enable memory watch, detect memory leak,
# similar to gmc, should disable in release version for hurts performance.
SRS_MEM_WATCH=NO
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
SRS_SENDMMSG=YES
SRS_HAS_SENDMMSG=YES
SRS_DEBUG=NO

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Presets:
  --x86-64, --x86-x64       [default] For x86/x64 cpu, common pc and servers.
  --arm                     Enable crossbuild for ARM, should also set bellow toolchain options.
  --mips                    Enable crossbuild for MIPS

Features:
  -h, --help                Print this message and exit 0.

  --with-ssl                Enable rtmp complex handshake, requires openssl-devel installed.
  --with-hds                Enable hds streaming, mux RTMP to F4M/F4V files.
  --with-stream-caster      Enable stream caster to serve other stream over other protocol.
  --with-stat               Enable the data statistic, for http api.
  --with-librtmp            Enable srs-librtmp, library for client.
  --with-research           Build the research tools.
  --with-utest              Build the utest for SRS.
  --with-srt                Build the SRT support for SRS.
  --with-rtc                Build the WebRTC support for SRS.
  --with-gb28181            Build the GB28181 support for SRS.

  --without-ssl             Disable rtmp complex handshake.
  --without-hds             Disable hds, the adobe http dynamic streaming.
  --without-stream-caster   Disable stream caster, only listen and serve RTMP/HTTP.
  --without-stat            Disable the data statistic feature.
  --without-librtmp         Disable srs-librtmp, library for client.
  --without-research        Disable the research tools.
  --without-utest           Disable the utest for SRS.
  --without-srt             Disable the SRT support for SRS.
  --without-rtc             Disable the WebRTC support for SRS.
  --without-gb28181         Disable the GB28181 support for SRS.

  --prefix=<path>           The absolute installation path for srs. Default: $SRS_PREFIX
  --static                  Whether add '-static' to link options.
  --gcov                    Whether enable the GCOV compiler options.
  --debug                   Whether enable the debug code, may hurt performance.
  --jobs[=N]                Allow N jobs at once; infinite jobs with no arg.
                            Used for make in the configure, for example, to make ffmpeg.
  --log-verbose             Whether enable the log verbose level. default: no.
  --log-info                Whether enable the log info level. default: no.
  --log-trace               Whether enable the log trace level. default: yes.

Performance:                @see https://blog.csdn.net/win_lin/article/details/53503869
  --with-valgrind           Support valgrind for memory check.
  --with-gperf              Build SRS with gperf tools(no gmd/gmc/gmp/gcp, with tcmalloc only).
  --with-gmc                Build memory check for SRS with gperf tools.
  --with-gmd                Build memory defense(corrupt memory) for SRS with gperf tools.
  --with-gmp                Build memory profile for SRS with gperf tools.
  --with-gcp                Build cpu profile for SRS with gperf tools.
  --with-gprof              Build SRS with gprof(GNU profile tool).

  --without-valgrind        Do not support valgrind for memory check.
  --without-gperf           Do not build SRS with gperf tools(without tcmalloc and gmd/gmc/gmp/gcp).
  --without-gmc             Do not build memory check for SRS with gperf tools.
  --without-gmd             Do not build memory defense for SRS with gperf tools.
  --without-gmp             Do not build memory profile for SRS with gperf tools.
  --without-gcp             Do not build cpu profile for SRS with gperf tools.
  --without-gprof           Do not build srs with gprof(GNU profile tool).

  --with-nasm               Build FFMPEG for RTC with nasm support.
  --without-nasm            Build FFMPEG for RTC without nasm support, for CentOS6 nasm is too old.
  --with-srtp-nasm          Build SRTP with ASM(openssl-asm) support, requires RTC and openssl-1.0.*.
  --without-srtp-nasm       Disable SRTP ASM support.
  --with-sendmmsg           Enable UDP sendmmsg support. @see http://man7.org/linux/man-pages/man2/sendmmsg.2.html
  --without-sendmmsg        Disable UDP sendmmsg support.

Toolchain options:          @see https://github.com/ossrs/srs/issues/1547#issuecomment-576078411
  --arm                     Enable crossbuild for ARM.
  --mips                    Enable crossbuild for MIPS.
  --cc=<CC>                 Use c compiler CC, default is gcc.
  --cxx=<CXX>               Use c++ compiler CXX, default is g++.
  --ar=<AR>                 Use archive tool AR, default is ar.
  --ld=<LD>                 Use linker tool LD, default is ld.
  --randlib=<RANDLIB>       Use randlib tool RANDLIB, default is randlib.
  --extra-flags=<EFLAGS>    Set EFLAGS as CFLAGS and CXXFLAGS. Also passed to ST as EXTRA_CFLAGS.

Conflicts:
  1. --with-gmc vs --with-gmp: 
        @see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html
  2. --with-gperf/gmc/gmp vs --with-gprof:
        The gperftools not compatible with gprof.
  3. --arm vs --with-ffmpeg/gperf/gmc/gmp/gprof:
        The complex tools not available for arm.

Experts:
  --use-sys-ssl                     Do not compile ssl, use system ssl(-lssl) if required.
  --use-shared-st                   Use link shared libraries for ST which uses MPL license.
  --use-shared-srt                  Use link shared libraries for SRT which uses MPL license.
  --export-librtmp-project=<path>   Export srs-librtmp to specified project in path.
  --export-librtmp-single=<path>    Export srs-librtmp to a single file(.h+.cpp) in path.
  --build-tag=<TAG>         Set the build object directory suffix.
  --with-clean              Configure SRS and do make clean if possible.
  --without-clean           Configure SRS and never make clean even possible..

Workflow:
  1. Apply "Presets". if not specified, use default preset.
  2. Apply "Features", "Performance" and others. user specified option will override the preset.
  3. Check conflicts, fail if exists conflicts.
  4. Generate Makefile.

Remark:
  1. For performance improving, read https://blog.csdn.net/win_lin/article/details/53503869

END
}

function parse_user_option() {
    case "$option" in
        -h)                             help=yes                    ;;
        --help)                         help=yes                    ;;
        
        --with-ssl)                     SRS_SSL=YES                 ;;
        --with-hds)                     SRS_HDS=YES                 ;;
        --with-nginx)                   SRS_NGINX=YES               ;;
        --with-ffmpeg)                  SRS_FFMPEG_TOOL=YES         ;;
        --with-transcode)               SRS_TRANSCODE=YES           ;;
        --with-ingest)                  SRS_INGEST=YES              ;;
        --with-stat)                    SRS_STAT=YES                ;;
        --with-stream-caster)           SRS_STREAM_CASTER=YES       ;;
        --with-librtmp)                 SRS_LIBRTMP=YES             ;;
        --with-research)                SRS_RESEARCH=YES            ;;
        --with-utest)                   SRS_UTEST=YES               ;;
        --with-srt)                     SRS_SRT=YES                 ;;
        --with-rtc)                     SRS_RTC=YES                 ;;
        --with-gb28181)                 SRS_GB28181=YES             ;;
        --with-nasm)                    SRS_NASM=YES                ;;
        --with-srtp-nasm)               SRS_SRTP_ASM=YES            ;;
        --with-sendmmsg)                SRS_SENDMMSG=YES            ;;
        --with-clean)                   SRS_CLEAN=YES               ;;
        --with-gperf)                   SRS_GPERF=YES               ;;
        --with-gmc)                     SRS_GPERF_MC=YES            ;;
        --with-gmd)                     SRS_GPERF_MD=YES            ;;
        --with-gmp)                     SRS_GPERF_MP=YES            ;;
        --with-gcp)                     SRS_GPERF_CP=YES            ;;
        --with-gprof)                   SRS_GPROF=YES               ;;
        --with-arm-ubuntu12)            SRS_CROSS_BUILD=YES         ;;
        --with-mips-ubuntu12)           SRS_CROSS_BUILD=YES         ;;

        --without-hds)                  SRS_HDS=NO                  ;;
        --without-nginx)                SRS_NGINX=NO                ;;
        --without-ffmpeg)               SRS_FFMPEG_TOOL=NO          ;;
        --without-librtmp)              SRS_LIBRTMP=NO              ;;
        --without-research)             SRS_RESEARCH=NO             ;;
        --without-utest)                SRS_UTEST=NO                ;;
        --without-srt)                  SRS_SRT=NO                  ;;
        --without-rtc)                  SRS_RTC=NO                  ;;
        --without-gb28181)              SRS_GB28181=NO              ;;
        --without-nasm)                 SRS_NASM=NO                 ;;
        --without-srtp-nasm)            SRS_SRTP_ASM=NO             ;;
        --without-sendmmsg)             SRS_SENDMMSG=NO             ;;
        --without-clean)                SRS_CLEAN=NO                ;;
        --without-gperf)                SRS_GPERF=NO                ;;
        --without-gmc)                  SRS_GPERF_MC=NO             ;;
        --without-gmd)                  SRS_GPERF_MD=NO             ;;
        --without-gmp)                  SRS_GPERF_MP=NO             ;;
        --without-gcp)                  SRS_GPERF_CP=NO             ;;
        --without-gprof)                SRS_GPROF=NO                ;;
        --without-arm-ubuntu12)         SRS_CROSS_BUILD=NO          ;;
        --without-mips-ubuntu12)        SRS_CROSS_BUILD=NO          ;;
        
        --jobs)                         SRS_JOBS=${value}           ;;
        --prefix)                       SRS_PREFIX=${value}         ;;
        --static)                       SRS_STATIC=YES              ;;
        --log-verbose)                  SRS_LOG_VERBOSE=YES         ;;
        --log-info)                     SRS_LOG_INFO=YES            ;;
        --log-trace)                    SRS_LOG_TRACE=YES           ;;
        --gcov)                         SRS_GCOV=YES                ;;
        --debug)                        SRS_DEBUG=YES               ;;

        --arm)                          SRS_CROSS_BUILD=YES         ;;
        --mips)                         SRS_CROSS_BUILD=YES         ;;
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
        --allow-osx)                    SRS_OSX=YES                 ;;
        --pi)                           SRS_PI=YES                  ;;
        --cubie)                        SRS_CUBIE=YES               ;;
        --dev)                          SRS_DEV=YES                 ;;
        --fast-dev)                     SRS_FAST_DEV=YES            ;;
        --demo)                         SRS_DEMO=YES                ;;
        --fast)                         SRS_FAST=YES                ;;
        --disable-all)                  SRS_DISABLE_ALL=YES         ;;
        --pure-rtmp)                    SRS_PURE_RTMP=YES           ;;
        --full)                         SRS_ENABLE_ALL=YES          ;;
        
        --use-sys-ssl)                  SRS_USE_SYS_SSL=YES         ;;
        --use-shared-st)                SRS_SHARED_ST=YES           ;;
        --use-shared-srt)               SRS_SHARED_SRT=YES          ;;

        --memory-watch)                 SRS_MEM_WATCH=YES           ;;
        --export-librtmp-project)       SRS_EXPORT_LIBRTMP_PROJECT=${value}     ;;
        --export-librtmp-single)        SRS_EXPORT_LIBRTMP_SINGLE=${value}      ;;
        --with-valgrind)                SRS_VALGRIND=YES            ;;
        --without-valgrind)             SRS_VALGRIND=NO             ;;

        --with-http-callback)           SRS_HTTP_CALLBACK=YES       ;;
        --with-http-api)                SRS_HTTP_API=YES            ;;
        --with-http-server)             SRS_HTTP_SERVER=YES         ;;
        --with-hls)                     SRS_HLS=YES                 ;;
        --with-dvr)                     SRS_DVR=YES                 ;;

        --without-stream-caster)        echo "ignore option \"$option\"" ;;
        --without-ingest)               echo "ignore option \"$option\"" ;;
        --without-ssl)                  echo "ignore option \"$option\"" ;;
        --without-stat)                 echo "ignore option \"$option\"" ;;
        --without-transcode)            echo "ignore option \"$option\"" ;;
        --without-http-callback)        echo "ignore option \"$option\"" ;;
        --without-http-server)          echo "ignore option \"$option\"" ;;
        --without-http-api)             echo "ignore option \"$option\"" ;;
        --without-hls)                  echo "ignore option \"$option\"" ;;
        --without-dvr)                  echo "ignore option \"$option\"" ;;

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

function apply_user_presets() {
    # always set the log level for all presets.
    SRS_LOG_VERBOSE=NO
    SRS_LOG_INFO=NO
    SRS_LOG_TRACE=YES
    
    # set default preset if not specifies
    if [[ $SRS_PURE_RTMP == NO && $SRS_FAST == NO && $SRS_DISABLE_ALL == NO && $SRS_ENABLE_ALL == NO && \
        $SRS_DEV == NO && $SRS_FAST_DEV == NO && $SRS_DEMO == NO && $SRS_PI == NO && $SRS_CUBIE == NO && \
        $SRS_X86_X64 == NO && $SRS_OSX == NO && $SRS_CROSS_BUILD == NO \
    ]]; then
        SRS_X86_X64=YES; opt="--x86-x64 $opt";
    fi

    # all disabled.
    if [ $SRS_DISABLE_ALL = YES ]; then
        SRS_HDS=NO
        SRS_LIBRTMP=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # all enabled.
    if [ $SRS_ENABLE_ALL = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=YES
        SRS_UTEST=YES
        SRS_STATIC=NO
    fi

    # only rtmp vp6
    if [ $SRS_FAST = YES ]; then
        SRS_HDS=NO
        SRS_LIBRTMP=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # only ssl for RTMP with complex handshake.
    if [ $SRS_PURE_RTMP = YES ]; then
        SRS_HDS=NO
        SRS_LIBRTMP=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # defaults for x86/x64
    if [ $SRS_X86_X64 = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # if dev specified, open features if possible.
    if [ $SRS_DEV = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=YES
        SRS_UTEST=YES
        SRS_STATIC=NO
    fi

    # if fast dev specified, open main server features.
    if [ $SRS_FAST_DEV = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi
	
    # for srs demo
    if [ $SRS_DEMO = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # if raspberry-pi specified, open ssl/hls/static features
    if [ $SRS_PI = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # if cubieboard specified, open features except ffmpeg/nginx.
    if [ $SRS_CUBIE = YES ]; then
        SRS_HDS=YES
        SRS_LIBRTMP=YES
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi

    # if crossbuild, disable research and librtmp.
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        SRS_LIBRTMP=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_STATIC=NO
    fi
}
apply_user_presets

#####################################################################################
# parse detail feature options
#####################################################################################
for option
do
    parse_user_option_to_value_and_option
    parse_user_option
done

function apply_user_detail_options() {
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
        SRS_EXPORT_LIBRTMP_PROJECT=$SRS_EXPORT_LIBRTMP_SINGLE
    fi
    
    # disable almost all features for export srs-librtmp.
    if [ $SRS_EXPORT_LIBRTMP_PROJECT != NO ]; then
        SRS_HDS=NO
        SRS_SSL=NO
        SRS_TRANSCODE=NO
        SRS_HTTP_CALLBACK=NO
        SRS_INGEST=NO
        SRS_STAT=NO
        SRS_STREAM_CASTER=NO
        SRS_LIBRTMP=YES
        SRS_RESEARCH=YES
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MD=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_RTC == NO ]]; then
        echo "Disable SRTP ASM, because RTC is disabled."
        SRS_SRTP_ASM=NO
    fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_NASM == NO ]]; then
        echo "Disable SRTP ASM, because NASM is disabled."
        SRS_SRTP_ASM=NO
    fi

    # Detect whether has sendmmsg.
    # @see http://man7.org/linux/man-pages/man2/sendmmsg.2.html
    mkdir -p ${SRS_OBJS} &&
    echo "  #include <sys/socket.h>           " > ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    echo "  int main(int argc, char** argv) { " >> ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    echo "    struct mmsghdr hdr;             " >> ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    echo "    hdr.msg_len = 0;                " >> ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    echo "    return 0;                       " >> ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    echo "  }                                 " >> ${SRS_OBJS}/_tmp_sendmmsg_detect.c
    ${SRS_TOOL_CC} -c ${SRS_OBJS}/_tmp_sendmmsg_detect.c -D_GNU_SOURCE -o /dev/null >/dev/null 2>&1
    ret=$?; rm -f ${SRS_OBJS}/_tmp_sendmmsg_detect.c;
    if [[ $ret -ne 0 ]]; then
        SRS_HAS_SENDMMSG=NO
        if [[ $SRS_SENDMMSG == YES ]]; then
          echo "Disable UDP sendmmsg automatically"
          SRS_SENDMMSG=NO
        fi
    fi
}
apply_user_detail_options

function regenerate_options() {
    # save all config options to macro to write to auto headers file
    SRS_AUTO_USER_CONFIGURE=`echo $opt`
    # regenerate the options for default values.
    SRS_AUTO_CONFIGURE="--prefix=${SRS_PREFIX}"
    if [ $SRS_HLS = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-hls"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-hls"; fi
    if [ $SRS_HDS = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-hds"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-hds"; fi
    if [ $SRS_DVR = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-dvr"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-dvr"; fi
    if [ $SRS_SSL = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-ssl"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-ssl"; fi
    if [ $SRS_TRANSCODE = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-transcode"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-transcode"; fi
    if [ $SRS_INGEST = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-ingest"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-ingest"; fi
    if [ $SRS_STAT = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-stat"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-stat"; fi
    if [ $SRS_HTTP_CALLBACK = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-callback"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-callback"; fi
    if [ $SRS_HTTP_SERVER = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-server"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-server"; fi
    if [ $SRS_STREAM_CASTER = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-stream-caster"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-stream-caster"; fi
    if [ $SRS_HTTP_API = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-api"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-api"; fi
    if [ $SRS_LIBRTMP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-librtmp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-librtmp"; fi
    if [ $SRS_RESEARCH = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-research"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-research"; fi
    if [ $SRS_UTEST = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-utest"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-utest"; fi
    if [ $SRS_SRT = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-srt"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-srt"; fi
    if [ $SRS_RTC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-rtc"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-rtc"; fi
    if [ $SRS_GB28181 = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gb28181"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gb28181"; fi
    if [ $SRS_NASM = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-nasm"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-nasm"; fi
    if [ $SRS_SRTP_ASM = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-srtp-nasm"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-srtp-nasm"; fi
    if [ $SRS_SENDMMSG = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-sendmmsg"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-sendmmsg"; fi
    if [ $SRS_CLEAN = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-clean"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-clean"; fi
    if [ $SRS_GPERF = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gperf"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gperf"; fi
    if [ $SRS_GPERF_MC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gmc"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gmc"; fi
    if [ $SRS_GPERF_MD = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gmd"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gmd"; fi
    if [ $SRS_GPERF_MP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gmp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gmp"; fi
    if [ $SRS_GPERF_CP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gcp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gcp"; fi
    if [ $SRS_GPROF = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gprof"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gprof"; fi
    if [ $SRS_STATIC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --static"; fi
    if [ $SRS_SHARED_ST = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --use-shared-st"; fi
    if [ $SRS_SHARED_SRT = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --use-shared-srt"; fi
    if [ $SRS_LOG_VERBOSE = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-verbose"; fi
    if [ $SRS_LOG_INFO = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-info"; fi
    if [ $SRS_LOG_TRACE = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-trace"; fi
    if [ $SRS_GCOV = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gcov"; fi
    if [ $SRS_DEBUG = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --debug"; fi
    if [[ $SRS_EXTRA_FLAGS != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --extra-flags=\\\"$SRS_EXTRA_FLAGS\\\""; fi
    if [[ $SRS_BUILD_TAG != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --build-tag=\\\"$SRS_BUILD_TAG\\\""; fi
    if [[ $SRS_TOOL_CC != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cc=$SRS_TOOL_CC"; fi
    if [[ $SRS_TOOL_CXX != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx=$SRS_TOOL_CXX"; fi
    if [[ $SRS_TOOL_AR != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ar=$SRS_TOOL_AR"; fi
    if [[ $SRS_TOOL_LD != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ld=$SRS_TOOL_LD"; fi
    if [[ $SRS_TOOL_RANDLIB != '' ]]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --randlib=$SRS_TOOL_RANDLIB"; fi
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
        echo "For crossbuild, must not use default toolchain, cc: $SRS_TOOL_CC, cxx: $SRS_TOOL_CXX, ar: $SRS_TOOL_AR"; exit -1
    fi

    if [[ $SRS_NGINX == YES ]]; then
        echo "Don't support building NGINX, please use docker https://github.com/ossrs/srs-docker"; exit -1;
    fi

    # For OSX, recommend to use DTrace, https://blog.csdn.net/win_lin/article/details/53503869
    if [[ $SRS_OSX == YES && $SRS_GPROF == YES ]]; then
        echo "Tool gprof for OSX is unavailable, please use dtrace, read https://blog.csdn.net/win_lin/article/details/53503869"
        exit -1
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
    if [ $SRS_LIBRTMP = RESERVED ]; then echo "you must specifies the librtmp, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_RESEARCH = RESERVED ]; then echo "you must specifies the research, see: ./configure --help"; __check_ok=NO; fi
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
