#!/bin/bash

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
# arguments
SRS_JOBS=1

# TODO: remove the default to yes.
SRS_HLS=YES
SRS_SSL=YES
SRS_FFMPEG=YES
SRS_HTTP_CALLBACK=YES
SRS_LIBRTMP=YES
SRS_BWTC=NO
SRS_RESEARCH=NO
SRS_UTEST=YES
SRS_GPERF=NO
SRS_GPERF_MC=NO
SRS_GPERF_MP=NO
SRS_GPERF_CP=NO
SRS_GPROF=NO

#####################################################################################
# parse options
#####################################################################################
opt=

for option
do
    opt="$opt `echo $option | sed -e \"s/\(--[^=]*=\)\(.* .*\)/\1'\2'/\"`"

    case "$option" in
        -*=*) 
            value=`echo "$option" | sed -e 's/[-_a-zA-Z0-9]*=//'` 
            option=`echo "$option" | sed -e 's/=[-_a-zA-Z0-9]*//'`
        ;;
           *) value="" ;;
    esac

    case "$option" in
        --help)                         help=yes                  ;;
        
        --with-ssl)                     SRS_SSL=YES               ;;
        --with-hls)                     SRS_HLS=YES               ;;
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
        
        --without-ssl)                  SRS_SSL=NO                ;;
        --without-hls)                  SRS_HLS=NO                ;;
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
        
        --jobs)                         SRS_JOBS=${value}         ;;

        *)
            echo "$0: error: invalid option \"$option\""
            exit 1
        ;;
    esac
done

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
  --with-http-callback              enable http hooks, build cherrypy as demo api server.
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

  --without-ssl            disable rtmp complex handshake.
  --without-hls            disable hls, rtmp streaming only.
  --without-http-callback           disable http, http hooks callback.
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
  
  --jobs[=N]               Allow N jobs at once; infinite jobs with no arg.
                           used for make in the configure, for example, to make ffmpeg.

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

# check variable neccessary
if [ $SRS_HLS = RESERVED ]; then echo "you must specifies the hls, see: ./configure --help"; __check_ok=NO; fi
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
if [ $__check_ok = NO ]; then
    exit 1;
fi

# regenerate the options for default values.
SRS_CONFIGURE=""
if [ $SRS_HLS = YES ]; then SRS_CONFIGURE="${SRS_CONFIGURE} --with-hls"; else SRS_CONFIGURE="${SRS_CONFIGURE} --without-hls"; fi
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
SRS_CONFIGURE="${SRS_CONFIGURE} ${SRS_JOBS}"
echo "regenerate config: ${SRS_CONFIGURE}"
