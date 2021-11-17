#!/bin/bash

################################################################
help=no
# feature options
SRS_HDS=NO
SRS_SRT=NO
SRS_RTC=YES
SRS_CXX11=NO
SRS_CXX14=NO
SRS_NGINX=NO
SRS_UTEST=NO
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
SRS_CHERRYPY=NO
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
# See https://github.com/ossrs/srs/wiki/LicenseMixing#state-threads
SRS_SHARED_ST=NO
# If enabled, link shared libraries for libsrt.so which uses MPL license.
# See https://github.com/ossrs/srs/wiki/LicenseMixing#srt
SRS_SHARED_SRT=NO
# If enabled, link shared libraries for FFmpeg which is LGPL license.
# See https://github.com/ossrs/srs/wiki/LicenseMixing#ffmpeg
SRS_SHARED_FFMPEG=NO
# whether enable the gcov
SRS_GCOV=NO
# whether enable the log verbose/info/trace level.
# always enable the warn/error level.
SRS_LOG_VERBOSE=NO
SRS_LOG_INFO=NO
SRS_LOG_TRACE=YES
#
################################################################
# Experts options.
SRS_USE_SYS_SSL=NO # Use system ssl(-lssl) if required.
SRS_VALGRIND=NO
SRS_BUILD_TAG= # Set the object files tag name.
SRS_CLEAN=YES # Whether do "make clean" when configure.
SRS_SIMULATOR=NO # Whether enable RTC simulate API.
#
################################################################
# Performance options.
SRS_GPERF=NO # Performance test: tcmalloc
SRS_GPERF_MC=NO # Performance test: gperf memory check
SRS_GPERF_MD=NO # Performance test: gperf memory defence
SRS_GPERF_MP=NO # Performance test: gperf memory profile
SRS_GPERF_CP=NO # Performance test: gperf cpu profile
SRS_GPROF=NO # Performance test: gprof
#
################################################################
# Preset options
SRS_X86_X64=NO # For x86_64 servers
SRS_OSX=NO #For osx/macOS PC.
SRS_CROSS_BUILD=NO #For cross build, for example, on Ubuntu.
# For cross build, the cpu, for example(FFmpeg), --cpu=24kc
SRS_CROSS_BUILD_CPU=
# For cross build, the arch, for example(FFmpeg), --arch=aarch64
SRS_CROSS_BUILD_ARCH=
# For cross build, the host, for example(libsrtp), --host=aarch64-linux-gnu
SRS_CROSS_BUILD_HOST=
# For cross build, the cross prefix, for example(FFmpeg), --cross-prefix=aarch64-linux-gnu-
SRS_CROSS_BUILD_PREFIX=
#
#####################################################################################
# Toolchain for cross-build on Ubuntu for ARM or MIPS.
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
SRS_DEBUG=NO
SRS_DEBUG_STATS=NO

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Presets:
  --x86-64, --x86-x64       For x86/x64 cpu, common pc and servers. Default: $(value2switch $SRS_X86_X64)
  --cross-build             Enable cross-build, please set bellow Toolchain also. Default: $(value2switch $SRS_CROSS_BUILD)
  --osx                     Enable build for OSX/Darwin AppleOS. Default: $(value2switch $SRS_OSX)

Features:
  -h, --help                Print this message and exit 0.

  --https=on|off            Whether enable HTTPS client and server. Default: $(value2switch $SRS_HTTPS)
  --hds=on|off              Whether build the hds streaming, mux RTMP to F4M/F4V files. Default: $(value2switch $SRS_HDS)
  --cherrypy=on|off         Whether install CherryPy for demo api-server. Default: $(value2switch $SRS_CHERRYPY)
  --utest=on|off            Whether build the utest. Default: $(value2switch $SRS_UTEST)
  --srt=on|off              Whether build the SRT. Default: $(value2switch $SRS_SRT)
  --rtc=on|off              Whether build the WebRTC. Default: $(value2switch $SRS_RTC)
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

Toolchain options:          @see https://github.com/ossrs/srs/wiki/v4_CN_SrsLinuxArm#ubuntu-cross-build-srs
  --static=on|off           Whether add '-static' to link options. Default: $(value2switch $SRS_STATIC)
  --cpu=<CPU>               Toolchain: Select the minimum required CPU for cross-build.
  --arch=<ARCH>             Toolchain: Select architecture for cross-build.
  --host=<BUILD>            Toolchain: Cross-compile to build programs to run on HOST.
  --cross-prefix=<PREFIX>   Toolchain: Use PREFIX for compilation tools.
  --cc=<CC>                 Toolchain: Use c compiler CC. Default: $SRS_TOOL_CC
  --cxx=<CXX>               Toolchain: Use c++ compiler CXX. Default: $SRS_TOOL_CXX
  --ar=<AR>                 Toolchain: Use archive tool AR. Default: $SRS_TOOL_CXX
  --ld=<LD>                 Toolchain: Use linker tool LD. Default: $SRS_TOOL_CXX
  --randlib=<RANDLIB>       Toolchain: Use randlib tool RANDLIB. Default: $SRS_TOOL_CXX
  --extra-flags=<EFLAGS>    Set EFLAGS as CFLAGS and CXXFLAGS. Also passed to ST as EXTRA_CFLAGS.

Experts:
  --sys-ssl=on|off          Do not compile ssl, use system ssl(-lssl) if required. Default: $(value2switch $SRS_USE_SYS_SSL)
  --ssl-1-0=on|off          Whether use openssl-1.0.*. Default: $(value2switch $SRS_SSL_1_0)
  --ssl-local=on|off        Whether use local openssl, not system even exists. Default: $(value2switch $SRS_SSL_LOCAL)
  --shared-st=on|off        Use shared libraries for ST which is MPL license. Default: $(value2switch $SRS_SHARED_ST)
  --shared-srt=on|off       Use shared libraries for SRT which is MPL license. Default: $(value2switch $SRS_SHARED_SRT)
  --shared-ffmpeg=on|off    Use shared libraries for FFmpeg which is LGPL license. Default: $(value2switch $SRS_SHARED_FFMPEG)
  --clean=on|off            Whether do 'make clean' when configure. Default: $(value2switch $SRS_CLEAN)
  --simulator=on|off        RTC: Whether enable network simulator. Default: $(value2switch $SRS_SIMULATOR)
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
    # Ignore the options.
    if [[ $option == '--demo' || $option == '--dev' || $option == '--fast-dev' || $option == '--pi'
      || $option == '--cubie' || $option == '--fast' || $option == '--pure-rtmp' || $option == '--disable-all'
      || $option == '--full' || $option == '--with-http-callback' || $option == '--without-http-callback'
      || $option == '--http-callback' || $option == '--with-http-api' || $option == '--without-http-api'
      || $option == '--http-api' || $option == '--with-http-server' || $option == '--without-http-server'
      || $option == '--http-server' || $option == '--with-hls' || $option == '--without-hls'
      || $option == '--hls' || $option == '--with-dvr' || $option == '--without-dvr'
      || $option == '--dvr' || $option == '--without-transcode' || $option == '--without-ingest'
      || $option == '--without-stat' || $option == '--without-stream-caster' || $option == '--without-ssl'
      || $option == '--without-librtmp' || ($option == '--librtmp' && $(switch2value $value) == NO)
      || $option == '--without-research' || ($option == '--research' && $(switch2value $value) == NO)
    ]]; then
        echo "Ignore option $option $value"; return 0;
    fi

    # if specified export single file, export project first.
    if [[ $option == '--export-librtmp-single' || $option == '--export-librtmp-project' || $option == '--with-librtmp' || $option == '--librtmp' ]]; then
        echo "Error: The $option is not supported yet, please read https://github.com/ossrs/srs-librtmp/issues/32"; exit 1
    fi

    if [[ $option == '--with-research' || $option == '--research' ]]; then
        echo "Error: The $option is not supported yet"; exit 1
    fi

    if [[ $option == '--arm' || $option == '--mips' || $option == '--with-arm-ubuntu12' || $option == '--with-mips-ubuntu12' ]]; then
        echo "Error: Removed misleading option $option, please read https://github.com/ossrs/srs/wiki/v4_CN_SrsLinuxArm#ubuntu-cross-build-srs"
        exit -1
    fi

    # Parse options to variables.
    case "$option" in
        -h)                             help=yes                    ;;
        --help)                         help=yes                    ;;
        
        --jobs)                         SRS_JOBS=${value}           ;;
        --prefix)                       SRS_PREFIX=${value}         ;;

        --static)                       SRS_STATIC=$(switch2value $value) ;;
        --cpu)                          SRS_CROSS_BUILD_CPU=${value} ;;
        --arch)                         SRS_CROSS_BUILD_ARCH=${value} ;;
        --host)                         SRS_CROSS_BUILD_HOST=${value} ;;
        --cross-prefix)                 SRS_CROSS_BUILD_PREFIX=${value} ;;
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

        --without-srtp-nasm)            SRS_SRTP_ASM=NO             ;;
        --with-srtp-nasm)               SRS_SRTP_ASM=YES            ;;
        --srtp-nasm)                    SRS_SRTP_ASM=$(switch2value $value) ;;

        --without-nasm)                 SRS_NASM=NO                 ;;
        --with-nasm)                    SRS_NASM=YES                ;;
        --nasm)                         SRS_NASM=$(switch2value $value) ;;

        --with-ssl)                     SRS_SSL=YES                 ;;
        --ssl)                          SRS_SSL=$(switch2value $value) ;;
        --https)                        SRS_HTTPS=$(switch2value $value) ;;
        --ssl-1-0)                      SRS_SSL_1_0=$(switch2value $value) ;;
        --ssl-local)                    SRS_SSL_LOCAL=$(switch2value $value) ;;

        --with-hds)                     SRS_HDS=YES                 ;;
        --without-hds)                  SRS_HDS=NO                  ;;
        --hds)                          SRS_HDS=$(switch2value $value) ;;

        --with-transcode)               SRS_TRANSCODE=YES           ;;
        --transcode)                    SRS_TRANSCODE=$(switch2value $value) ;;

        --with-ingest)                  SRS_INGEST=YES              ;;
        --ingest)                       SRS_INGEST=$(switch2value $value) ;;

        --with-stat)                    SRS_STAT=YES                ;;
        --stat)                         SRS_STAT=$(switch2value $value) ;;

        --with-stream-caster)           SRS_STREAM_CASTER=YES       ;;
        --stream-caster)                SRS_STREAM_CASTER=$(switch2value $value) ;;

        --with-utest)                   SRS_UTEST=YES               ;;
        --without-utest)                SRS_UTEST=NO                ;;
        --utest)                        SRS_UTEST=$(switch2value $value) ;;
        --cherrypy)                     SRS_CHERRYPY=$(switch2value $value) ;;
        --gcov)                         SRS_GCOV=$(switch2value $value) ;;

        --with-srt)                     SRS_SRT=YES                 ;;
        --without-srt)                  SRS_SRT=NO                  ;;
        --srt)                          SRS_SRT=$(switch2value $value) ;;

        --with-rtc)                     SRS_RTC=YES                 ;;
        --without-rtc)                  SRS_RTC=NO                  ;;
        --rtc)                          SRS_RTC=$(switch2value $value) ;;
        --simulator)                    SRS_SIMULATOR=$(switch2value $value) ;;
        --ffmpeg-fit)                   SRS_FFMPEG_FIT=$(switch2value $value) ;;

        --cxx11)                        SRS_CXX11=$(switch2value $value) ;;
        --cxx14)                        SRS_CXX14=$(switch2value $value) ;;

        --with-clean)                   SRS_CLEAN=YES               ;;
        --without-clean)                SRS_CLEAN=NO                ;;
        --clean)                        SRS_CLEAN=$(switch2value $value) ;;

        --with-gperf)                   SRS_GPERF=YES               ;;
        --without-gperf)                SRS_GPERF=NO                ;;
        --gperf)                        SRS_GPERF=$(switch2value $value) ;;

        --with-gmc)                     SRS_GPERF_MC=YES            ;;
        --without-gmc)                  SRS_GPERF_MC=NO             ;;
        --gmc)                          SRS_GPERF_MC=$(switch2value $value) ;;

        --with-gmd)                     SRS_GPERF_MD=YES            ;;
        --without-gmd)                  SRS_GPERF_MD=NO             ;;
        --gmd)                          SRS_GPERF_MD=$(switch2value $value) ;;

        --with-gmp)                     SRS_GPERF_MP=YES            ;;
        --without-gmp)                  SRS_GPERF_MP=NO             ;;
        --gmp)                          SRS_GPERF_MP=$(switch2value $value) ;;

        --with-gcp)                     SRS_GPERF_CP=YES            ;;
        --without-gcp)                  SRS_GPERF_CP=NO             ;;
        --gcp)                          SRS_GPERF_CP=$(switch2value $value) ;;

        --with-gprof)                   SRS_GPROF=YES               ;;
        --without-gprof)                SRS_GPROF=NO                ;;
        --gprof)                        SRS_GPROF=$(switch2value $value) ;;

        --use-sys-ssl)                  SRS_USE_SYS_SSL=YES         ;;
        --sys-ssl)                      SRS_USE_SYS_SSL=$(switch2value $value) ;;

        --use-shared-st)                SRS_SHARED_ST=YES           ;;
        --use-shared-srt)               SRS_SHARED_SRT=YES          ;;
        --shared-st)                    SRS_SHARED_ST=$(switch2value $value) ;;
        --shared-srt)                   SRS_SHARED_SRT=$(switch2value $value) ;;
        --shared-ffmpeg)                SRS_SHARED_FFMPEG=$(switch2value $value) ;;

        --with-valgrind)                SRS_VALGRIND=YES            ;;
        --without-valgrind)             SRS_VALGRIND=NO             ;;
        --valgrind)                     SRS_VALGRIND=$(switch2value $value) ;;

        --log-verbose)                  SRS_LOG_VERBOSE=$(switch2value $value) ;;
        --log-info)                     SRS_LOG_INFO=$(switch2value $value) ;;
        --log-trace)                    SRS_LOG_TRACE=$(switch2value $value) ;;
        --debug)                        SRS_DEBUG=$(switch2value $value) ;;
        --debug-stats)                  SRS_DEBUG_STATS=$(switch2value $value) ;;

        # Alias for --arm, cross build.
        --cross-build)                  SRS_CROSS_BUILD=YES         ;;
        --enable-cross-compile)         SRS_CROSS_BUILD=YES         ;;

        # Deprecated, might be removed in future.
        --with-nginx)                   SRS_NGINX=YES               ;;
        --without-nginx)                SRS_NGINX=NO                ;;
        --nginx)                        SRS_NGINX=$(switch2value $value) ;;
        --with-ffmpeg)                  SRS_FFMPEG_TOOL=YES         ;;
        --without-ffmpeg)               SRS_FFMPEG_TOOL=NO          ;;
        --ffmpeg)                       SRS_FFMPEG_TOOL=$(switch2value $value) ;;
        --ffmpeg-tool)                  SRS_FFMPEG_TOOL=$(switch2value $value) ;;

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

# For variable values, might be three values: YES, RESERVED, NO(by default).
function value2switch() {
    if [[ $1 == YES ]]; then
      echo on;
    else
      echo off;
    fi
}

# For user options, only off or on(by default).
function switch2value() {
    if [[ $1 == off ]]; then
      echo NO;
    else
      echo YES;
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

function apply_auto_options() {
    # set default preset if not specifies
    if [[ $SRS_X86_X64 == NO && $SRS_OSX == NO && $SRS_CROSS_BUILD == NO ]]; then
        SRS_X86_X64=YES; opt="--x86-x64 $opt";
    fi

    if [[ $SRS_CROSS_BUILD == YES ]]; then
        if [[ $SRS_CROSS_BUILD_PREFIX != "" && $SRS_CROSS_BUILD_HOST == "" ]]; then
            SRS_CROSS_BUILD_HOST=$(echo $SRS_CROSS_BUILD_PREFIX| sed 's/-$//g')
        fi
        if [[ $SRS_TOOL_CC != "" && $SRS_CROSS_BUILD_HOST == "" ]]; then
            SRS_CROSS_BUILD_HOST=$(echo $SRS_TOOL_CC| sed 's/-gcc$//g')
        fi
        if [[ $SRS_CROSS_BUILD_PREFIX == "" ]]; then
            SRS_CROSS_BUILD_PREFIX="${SRS_CROSS_BUILD_HOST}-"
        fi
        SRS_TOOL_CC=${SRS_CROSS_BUILD_PREFIX}gcc
        SRS_TOOL_CXX=${SRS_CROSS_BUILD_PREFIX}g++
        SRS_TOOL_AR=${SRS_CROSS_BUILD_PREFIX}ar
        SRS_TOOL_LD=${SRS_CROSS_BUILD_PREFIX}ld
        SRS_TOOL_RANDLIB=${SRS_CROSS_BUILD_PREFIX}randlib
        if [[ $SRS_CROSS_BUILD_ARCH == "" ]]; then
            echo $SRS_TOOL_CC| grep arm >/dev/null 2>&1 && SRS_CROSS_BUILD_ARCH="arm"
            echo $SRS_TOOL_CC| grep aarch64 >/dev/null 2>&1 && SRS_CROSS_BUILD_ARCH="aarch64"
        fi
        echo "For cross build, host: $SRS_CROSS_BUILD_HOST, prefix: $SRS_CROSS_BUILD_PREFIX, arch: $SRS_CROSS_BUILD_ARCH, cpu: $SRS_CROSS_BUILD_CPU gcc: $SRS_TOOL_CC"
    fi

    if [[ $SRS_OSX == YES ]]; then
      SRS_TOOL_LD=$SRS_TOOL_CC
    fi

    # The SRT code in SRS requires c++11, although we build libsrt without c++11.
    # TODO: FIXME: Remove c++11 code in SRT of SRS.
    if [[ $SRS_SRT == YES ]]; then
        SRS_CXX11=YES
    fi

    # Enable FFmpeg fit for RTC to transcode audio from AAC to OPUS, if user enabled it.
    if [[ $SRS_RTC == YES && $SRS_FFMPEG_FIT == RESERVED ]]; then
        SRS_FFMPEG_FIT=YES
    fi

    # if transcode/ingest specified, requires the ffmpeg stub classes.
    SRS_FFMPEG_STUB=NO
    if [ $SRS_TRANSCODE = YES ]; then SRS_FFMPEG_STUB=YES; fi
    if [ $SRS_INGEST = YES ]; then SRS_FFMPEG_STUB=YES; fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_RTC == NO ]]; then
        echo "Disable SRTP-ASM, because RTC is disabled."
        SRS_SRTP_ASM=NO
    fi

    if [[ $SRS_SRTP_ASM == YES && $SRS_NASM == NO ]]; then
        echo "Disable SRTP-ASM, because NASM is disabled."
        SRS_SRTP_ASM=NO
    fi
}

if [ $help = yes ]; then
    apply_auto_options
    show_help
    exit 0
fi

#####################################################################################
# apply options
#####################################################################################

function apply_detail_options() {
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
}
apply_auto_options
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
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx11=$(value2switch $SRS_CXX11)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cxx14=$(value2switch $SRS_CXX14)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --ffmpeg-fit=$(value2switch $SRS_FFMPEG_FIT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --nasm=$(value2switch $SRS_NASM)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --srtp-nasm=$(value2switch $SRS_SRTP_ASM)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --clean=$(value2switch $SRS_CLEAN)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gperf=$(value2switch $SRS_GPERF)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmc=$(value2switch $SRS_GPERF_MC)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmd=$(value2switch $SRS_GPERF_MD)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gmp=$(value2switch $SRS_GPERF_MP)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gcp=$(value2switch $SRS_GPERF_CP)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gprof=$(value2switch $SRS_GPROF)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --static=$(value2switch $SRS_STATIC)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --shared-st=$(value2switch $SRS_SHARED_ST)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --shared-srt=$(value2switch $SRS_SHARED_SRT)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --shared-ffmpeg=$(value2switch $SRS_SHARED_FFMPEG)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-verbose=$(value2switch $SRS_LOG_VERBOSE)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-info=$(value2switch $SRS_LOG_INFO)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --log-trace=$(value2switch $SRS_LOG_TRACE)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --gcov=$(value2switch $SRS_GCOV)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --debug=$(value2switch $SRS_DEBUG)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --debug-stats=$(value2switch $SRS_DEBUG_STATS)"
    SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --cross-build=$(value2switch $SRS_CROSS_BUILD)"
    if [[ $SRS_CROSS_BUILD_ARCH != "" ]]; then SRS_AUTO_CONFIGURE="$SRS_AUTO_CONFIGURE --arch=$SRS_CROSS_BUILD_ARCH"; fi
    if [[ $SRS_CROSS_BUILD_CPU != "" ]]; then SRS_AUTO_CONFIGURE="$SRS_AUTO_CONFIGURE --cpu=$SRS_CROSS_BUILD_CPU"; fi
    if [[ $SRS_CROSS_BUILD_HOST != "" ]]; then SRS_AUTO_CONFIGURE="$SRS_AUTO_CONFIGURE --host=$SRS_CROSS_BUILD_HOST"; fi
    if [[ $SRS_CROSS_BUILD_PREFIX != "" ]]; then SRS_AUTO_CONFIGURE="$SRS_AUTO_CONFIGURE --cross-prefix=$SRS_CROSS_BUILD_PREFIX"; fi
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
        echo "Error: No build toolchain, cc: $SRS_TOOL_CC, cxx: $SRS_TOOL_CXX, ar: $SRS_TOOL_AR, ld: $SRS_TOOL_LD, randlib: $SRS_TOOL_RANDLIB"; exit -1
    fi

    if [[ $SRS_CROSS_BUILD == YES && ($SRS_TOOL_CC == 'gcc' || $SRS_TOOL_CXX == 'g++' || $SRS_TOOL_AR == 'ar') ]]; then
        echo "Error: For cross build, should setup the toolchain(./configure -h|grep -i toolchain), cc: $SRS_TOOL_CC, cxx: $SRS_TOOL_CXX, ar: $SRS_TOOL_AR"; exit 1
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
        echo "@see: https://gperftools.github.io/gperftools/heap_checker.html";
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
