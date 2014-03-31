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

SRS_HLS=RESERVED
SRS_NGINX=RESERVED
SRS_SSL=RESERVED
SRS_FFMPEG=RESERVED
SRS_HTTP_CALLBACK=RESERVED
SRS_LIBRTMP=RESERVED # srs-librtmp
SRS_BWTC=RESERVED # srs-bandwidth-test client
SRS_RESEARCH=RESERVED
SRS_UTEST=RESERVED
SRS_GPERF=RESERVED # tcmalloc
SRS_GPERF_MC=RESERVED # gperf memory check
SRS_GPERF_MP=RESERVED # gperf memory profile
SRS_GPERF_CP=RESERVED # gperf cpu profile
SRS_GPROF=RESERVED # gprof
SRS_ARM_UBUNTU12=RESERVED # armhf(v7cpu) built on ubuntu12
# arguments
SRS_PREFIX=/usr/local/srs
SRS_JOBS=1
SRS_STATIC=RESERVED
# private internal
# dev, open all features for dev, no gperf/prof/arm.
SRS_DEV=NO
# raspberry-pi, open hls/ssl/static
SRS_PI=NO

#####################################################################################
# parse options
#####################################################################################
opt=

for option
do
    opt="$opt `echo $option | sed -e \"s/\(--[^=]*=\)\(.* .*\)/\1'\2'/\"`"

    case "$option" in
        -*=*) 
            value=`echo "$option" | sed -e 's|[-_a-zA-Z0-9/]*=||'` 
            option=`echo "$option" | sed -e 's|=[-_a-zA-Z0-9/]*||'`
        ;;
           *) value="" ;;
    esac

    case "$option" in
        --help)                         help=yes                  ;;
        
        --with-ssl)                     SRS_SSL=YES               ;;
        --with-hls)                     SRS_HLS=YES               ;;
        --with-nginx)                   SRS_NGINX=YES             ;;
        --with-ffmpeg)                  SRS_FFMPEG=YES            ;;
        --with-http-callback)           SRS_HTTP_CALLBACK=YES     ;;
        --with-librtmp)                 SRS_LIBRTMP=YES           ;;
        --with-bwtc)                    SRS_BWTC=YES              ;;
        --with-research)                SRS_RESEARCH=YES          ;;
        --with-utest)                   SRS_UTEST=YES             ;;
        --with-gperf)                   SRS_GPERF=YES             ;;
        --with-gmc)                     SRS_GPERF_MC=YES          ;;
        --with-gmp)                     SRS_GPERF_MP=YES          ;;
        --with-gcp)                     SRS_GPERF_CP=YES          ;;
        --with-gprof)                   SRS_GPROF=YES             ;;
        --with-arm-ubuntu12)            SRS_ARM_UBUNTU12=YES      ;;
        
        --without-ssl)                  SRS_SSL=NO                ;;
        --without-hls)                  SRS_HLS=NO                ;;
        --without-nginx)                SRS_NGINX=NO              ;;
        --without-ffmpeg)               SRS_FFMPEG=NO             ;;
        --without-http-callback)        SRS_HTTP_CALLBACK=NO      ;;
        --without-librtmp)              SRS_LIBRTMP=NO            ;;
        --without-bwtc)                 SRS_BWTC=NO               ;;
        --without-research)             SRS_RESEARCH=NO           ;;
        --without-utest)                SRS_UTEST=NO              ;;
        --without-gperf)                SRS_GPERF=NO              ;;
        --without-gmc)                  SRS_GPERF_MC=NO           ;;
        --without-gmp)                  SRS_GPERF_MP=NO           ;;
        --without-gcp)                  SRS_GPERF_CP=NO           ;;
        --without-gprof)                SRS_GPROF=NO              ;;
        --without-arm-ubuntu12)         SRS_ARM_UBUNTU12=NO       ;;
        
        --jobs)                         SRS_JOBS=${value}         ;;
        --prefix)                       SRS_PREFIX=${value}       ;;
        --static)                       SRS_STATIC=YES            ;;
        --dev)                          SRS_DEV=YES               ;;
        --pi)                           SRS_PI=YES                ;;

        *)
            echo "$0: error: invalid option \"$option\""
            exit 1
        ;;
    esac
done

#####################################################################################
# apply the default value when user donot specified.
#####################################################################################
# if arm specified, set some default to disabled.
if [ $SRS_ARM_UBUNTU12 = YES ]; then
    if [ $SRS_HLS = RESERVED ]; then SRS_HLS=NO; fi
    if [ $SRS_NGINX = RESERVED ]; then SRS_NGINX=NO; fi
    if [ $SRS_SSL = RESERVED ]; then SRS_SSL=NO; fi
    if [ $SRS_FFMPEG = RESERVED ]; then SRS_FFMPEG=NO; fi
    if [ $SRS_HTTP_CALLBACK = RESERVED ]; then SRS_HTTP_CALLBACK=NO; fi
    if [ $SRS_LIBRTMP = RESERVED ]; then SRS_LIBRTMP=NO; fi
    if [ $SRS_BWTC = RESERVED ]; then SRS_BWTC=NO; fi
    if [ $SRS_RESEARCH = RESERVED ]; then SRS_RESEARCH=NO; fi
    if [ $SRS_UTEST = RESERVED ]; then SRS_UTEST=NO; fi
    if [ $SRS_GPERF = RESERVED ]; then SRS_GPERF=NO; fi
    if [ $SRS_GPERF_MC = RESERVED ]; then SRS_GPERF_MC=NO; fi
    if [ $SRS_GPERF_MP = RESERVED ]; then SRS_GPERF_MP=NO; fi
    if [ $SRS_GPERF_CP = RESERVED ]; then SRS_GPERF_CP=NO; fi
    if [ $SRS_GPROF = RESERVED ]; then SRS_GPROF=NO; fi
    if [ $SRS_ARM_UBUNTU12 = RESERVED ]; then SRS_ARM_UBUNTU12=NO; fi
    if [ $SRS_DEV = RESERVED ]; then SRS_DEV=NO; fi
    if [ $SRS_PI = RESERVED ]; then SRS_PI=NO; fi
    # for arm, always set to static link.
    SRS_STATIC=YES
else
    if [ $SRS_HLS = RESERVED ]; then SRS_HLS=YES; fi
    if [ $SRS_NGINX = RESERVED ]; then SRS_NGINX=NO; fi
    if [ $SRS_SSL = RESERVED ]; then SRS_SSL=YES; fi
    if [ $SRS_FFMPEG = RESERVED ]; then SRS_FFMPEG=NO; fi
    if [ $SRS_HTTP_CALLBACK = RESERVED ]; then SRS_HTTP_CALLBACK=NO; fi
    if [ $SRS_LIBRTMP = RESERVED ]; then SRS_LIBRTMP=NO; fi
    if [ $SRS_BWTC = RESERVED ]; then SRS_BWTC=NO; fi
    if [ $SRS_RESEARCH = RESERVED ]; then SRS_RESEARCH=NO; fi
    if [ $SRS_UTEST = RESERVED ]; then SRS_UTEST=NO; fi
    if [ $SRS_GPERF = RESERVED ]; then SRS_GPERF=NO; fi
    if [ $SRS_GPERF_MC = RESERVED ]; then SRS_GPERF_MC=NO; fi
    if [ $SRS_GPERF_MP = RESERVED ]; then SRS_GPERF_MP=NO; fi
    if [ $SRS_GPERF_CP = RESERVED ]; then SRS_GPERF_CP=NO; fi
    if [ $SRS_GPROF = RESERVED ]; then SRS_GPROF=NO; fi
    if [ $SRS_ARM_UBUNTU12 = RESERVED ]; then SRS_ARM_UBUNTU12=NO; fi
    if [ $SRS_STATIC = RESERVED ]; then SRS_STATIC=NO; fi
    if [ $SRS_DEV = RESERVED ]; then SRS_DEV=NO; fi
    if [ $SRS_PI = RESERVED ]; then SRS_PI=NO; fi
fi

# if dev specified, open features if possible.
if [ $SRS_DEV = YES ]; then
    SRS_HLS=YES
    SRS_NGINX=YES
    SRS_SSL=YES
    SRS_FFMPEG=YES
    SRS_HTTP_CALLBACK=YES
    SRS_LIBRTMP=YES
    SRS_BWTC=YES
    SRS_RESEARCH=YES
    SRS_UTEST=YES
    if [ $SRS_GPERF = RESERVED ]; then SRS_GPERF=NO; fi
    if [ $SRS_GPERF_MC = RESERVED ]; then SRS_GPERF_MC=NO; fi
    if [ $SRS_GPERF_MP = RESERVED ]; then SRS_GPERF_MP=NO; fi
    if [ $SRS_GPERF_CP = RESERVED ]; then SRS_GPERF_CP=NO; fi
    if [ $SRS_GPROF = RESERVED ]; then SRS_GPROF=NO; fi
    SRS_ARM_UBUNTU12=NO
    # for arm, always set to static link.
    SRS_STATIC=NO
fi

# if raspberry-pi specified, open ssl/hls/static features
if [ $SRS_PI = YES ]; then
    SRS_HLS=YES
    SRS_NGINX=NO
    SRS_SSL=YES
    SRS_FFMPEG=NO
    SRS_HTTP_CALLBACK=NO
    SRS_LIBRTMP=NO
    SRS_BWTC=NO
    SRS_RESEARCH=NO
    SRS_UTEST=NO
    if [ $SRS_GPERF = RESERVED ]; then SRS_GPERF=NO; fi
    if [ $SRS_GPERF_MC = RESERVED ]; then SRS_GPERF_MC=NO; fi
    if [ $SRS_GPERF_MP = RESERVED ]; then SRS_GPERF_MP=NO; fi
    if [ $SRS_GPERF_CP = RESERVED ]; then SRS_GPERF_CP=NO; fi
    if [ $SRS_GPROF = RESERVED ]; then SRS_GPROF=NO; fi
    SRS_ARM_UBUNTU12=NO
    # for arm, always set to static link.
    SRS_STATIC=YES
fi

# parse the jobs for make
if [[ "" -eq SRS_JOBS ]]; then 
    export SRS_JOBS="--jobs" 
else
    export SRS_JOBS="--jobs=${SRS_JOBS}"
fi

# save all config options to macro to write to auto headers file
SRS_CONFIGURE="$opt"

#####################################################################################
# show help and exit
#####################################################################################
if [ $help = yes ]; then
    cat << END

  --help                   print this message

  --with-ssl               enable rtmp complex handshake, requires openssl-devel installed.
                           to delivery h264 video and aac audio to flash player.
  --with-hls               enable hls streaming, build nginx as http server for hls.
  --with-http-callback     enable http hooks, build cherrypy as demo api server.
                           srs will call the http hooks, such as: on_connect.
  --with-ffmpeg            enable transcoding with ffmpeg.
  --with-librtmp           enable srs-librtmp, library for client.
  --with-bwtc              enable srs bandwidth test client tool.
  --with-research          build the research tools.
  --with-utest             build the utest for srs.
  --with-gperf             build srs with gperf tools(no gmc/gmp/gcp, with tcmalloc only).
  --with-gmc               build memory check for srs with gperf tools.
  --with-gmp               build memory profile for srs with gperf tools.
  --with-gcp               build cpu profile for srs with gperf tools.
  --with-gprof             build srs with gprof(GNU profile tool).
  --with-arm-ubuntu12      build srs on ubuntu12 for armhf(v7cpu).

  --without-ssl            disable rtmp complex handshake.
  --without-hls            disable hls, rtmp streaming only.
  --without-http-callback  disable http, http hooks callback.
  --without-ffmpeg         disable the ffmpeg transcoding feature.
  --without-librtmp        disable srs-librtmp, library for client.
  --without-bwtc           disable srs bandwidth test client tool.
  --without-research       do not build the research tools.
  --without-utest          do not build the utest for srs.
  --without-gperf          do not build srs with gperf tools(without tcmalloc and gmc/gmp/gcp).
  --without-gmc            do not build memory check for srs with gperf tools.
  --without-gmp            do not build memory profile for srs with gperf tools.
  --without-gcp            do not build cpu profile for srs with gperf tools.
  --without-gprof          do not build srs with gprof(GNU profile tool).
  --without-arm-ubuntu12   do not build srs on ubuntu12 for armhf(v7cpu).
  
  --static                 whether add '-static' to link options. always set this option for arm.
  --jobs[=N]               Allow N jobs at once; infinite jobs with no arg.
                           used for make in the configure, for example, to make ffmpeg.
  --prefix=<path>          the absolute install path for srs.
  --dev                    for dev, open all features, no gperf/gprof/arm.
  --pi                     for raspberry-pi(directly build), open features hls/ssl/static.

END
    exit 0
fi

#####################################################################################
# check user options
#####################################################################################
__check_ok=YES
# check conflict
if [ $SRS_GPERF = NO ]; then
    if [ $SRS_GPERF_MC = YES ]; then echo "gperf-mc depends on gperf, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MP = YES ]; then echo "gperf-mp depends on gperf, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_CP = YES ]; then echo "gperf-cp depends on gperf, see: ./configure --help"; __check_ok=NO; fi
fi
if [ $SRS_GPERF_MC = YES ]; then
    if [ $SRS_GPERF_MP = YES ]; then
        echo "gperf-mc not compatible with gperf-mp, see: ./configure --help";
        echo "@see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html";
        echo "Note that since the heap-checker uses the heap-profiling framework internally, it is not possible to run both the heap-checker and heap profiler at the same time";
        __check_ok=NO
    fi
fi
# generate the group option: SRS_GPERF
__gperf_slow=NO
if [ $SRS_GPERF_MC = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
if [ $SRS_GPERF_MP = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
if [ $SRS_GPERF_CP = YES ]; then SRS_GPERF=YES; __gperf_slow=YES; fi
if [ $__gperf_slow = YES ]; then if [ $SRS_GPROF = YES ]; then 
    echo "gmc/gmp/gcp not compatible with gprof, see: ./configure --help"; __check_ok=NO; 
fi fi

# check arm, if arm enabled, only allow st/ssl/librtmp,
# user should disable all other features
if [ $SRS_ARM_UBUNTU12 = YES ]; then
    if [ $SRS_FFMPEG = YES ]; then echo "ffmpeg for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_HTTP_CALLBACK = YES ]; then echo "http-callback for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_BWTC = YES ]; then echo "bwtc for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_RESEARCH = YES ]; then echo "research for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF = YES ]; then echo "gperf for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MC = YES ]; then echo "gmc for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_MP = YES ]; then echo "gmp for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPERF_CP = YES ]; then echo "gcp for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_GPROF = YES ]; then echo "gprof for arm is not available, see: ./configure --help"; __check_ok=NO; fi
fi

# check variable neccessary
if [ $SRS_HLS = RESERVED ]; then echo "you must specifies the hls, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_NGINX = RESERVED ]; then echo "you must specifies the nginx, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_SSL = RESERVED ]; then echo "you must specifies the ssl, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_FFMPEG = RESERVED ]; then echo "you must specifies the ffmpeg, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_HTTP_CALLBACK = RESERVED ]; then echo "you must specifies the http, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_LIBRTMP = RESERVED ]; then echo "you must specifies the librtmp, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_BWTC = RESERVED ]; then echo "you must specifies the bwtc, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_RESEARCH = RESERVED ]; then echo "you must specifies the research, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_UTEST = RESERVED ]; then echo "you must specifies the utest, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_GPERF = RESERVED ]; then echo "you must specifies the gperf, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_GPERF_MC = RESERVED ]; then echo "you must specifies the gperf-mc, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_GPERF_MP = RESERVED ]; then echo "you must specifies the gperf-mp, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_GPERF_CP = RESERVED ]; then echo "you must specifies the gperf-cp, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_GPROF = RESERVED ]; then echo "you must specifies the gprof, see: ./configure --help"; __check_ok=NO; fi
if [ $SRS_ARM_UBUNTU12 = RESERVED ]; then echo "you must specifies the arm-ubuntu12, see: ./configure --help"; __check_ok=NO; fi
if [[ -z $SRS_PREFIX ]]; then echo "you must specifies the prefix, see: ./configure --prefix"; __check_ok=NO; fi
if [ $__check_ok = NO ]; then
    exit 1;
fi

# regenerate the options for default values.
SRS_CONFIGURE=""
if [ $SRS_DEV = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --dev"; fi
if [ $SRS_PI = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --pi"; fi
if [ $SRS_HLS = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-hls"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-hls"; fi
if [ $SRS_NGINX = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-nginx"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-nginx"; fi
if [ $SRS_SSL = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-ssl"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-ssl"; fi
if [ $SRS_FFMPEG = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-ffmpeg"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-ffmpeg"; fi
if [ $SRS_HTTP_CALLBACK = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-http-callback"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-http-callback"; fi
if [ $SRS_LIBRTMP = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-librtmp"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-librtmp"; fi
if [ $SRS_BWTC = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-bwtc"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-bwtc"; fi
if [ $SRS_RESEARCH = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-research"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-research"; fi
if [ $SRS_UTEST = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-utest"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-utest"; fi
if [ $SRS_GPERF = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-gperf"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-gperf"; fi
if [ $SRS_GPERF_MC = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-gmc"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-gmc"; fi
if [ $SRS_GPERF_MP = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-gmp"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-gmp"; fi
if [ $SRS_GPERF_CP = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-gcp"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-gcp"; fi
if [ $SRS_GPROF = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-gprof"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-gprof"; fi
if [ $SRS_ARM_UBUNTU12 = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-arm-ubuntu12"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-arm-ubuntu12"; fi
if [ $SRS_STATIC = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --static"; fi
SRS_CONFIGURE="${SRS_CONFIGURE} ${SRS_JOBS} --prefix=${SRS_PREFIX}"
echo "regenerate config: ${SRS_CONFIGURE}"
