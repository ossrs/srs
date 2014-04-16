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
SRS_DVR=RESERVED
SRS_NGINX=RESERVED
SRS_SSL=RESERVED
SRS_FFMPEG_TOOL=RESERVED
SRS_TRANSCODE=RESERVED
SRS_INGEST=RESERVED
SRS_HTTP_CALLBACK=RESERVED
SRS_HTTP_SERVER=RESERVED
SRS_HTTP_API=RESERVED
SRS_LIBRTMP=RESERVED
# srs-bandwidth-test client
SRS_BWTC=RESERVED
SRS_RESEARCH=RESERVED
SRS_UTEST=RESERVED
# tcmalloc
SRS_GPERF=RESERVED
# gperf memory check
SRS_GPERF_MC=RESERVED
# gperf memory profile
SRS_GPERF_MP=RESERVED
# gperf cpu profile
SRS_GPERF_CP=RESERVED
# gprof
SRS_GPROF=RESERVED
# 
# libraries
SRS_FFMPEG_STUB=RESERVED
SRS_HTTP_PARSER=RESERVED
# arguments
SRS_PREFIX=/usr/local/srs
SRS_JOBS=1
SRS_STATIC=RESERVED
#
# presets
# for x86/x64 pc/servers
SRS_X86_X64=NO
# armhf(v7cpu) built on ubuntu12
SRS_ARM_UBUNTU12=NO
# dev, open all features for dev, no gperf/prof/arm.
SRS_DEV=NO
# raspberry-pi, open hls/ssl/static
SRS_PI=NO
# the most fast compile, nothing, only support vp6 RTMP.
SRS_FAST=NO
# only support RTMP with ssl.
SRS_PURE_RTMP=NO
# only support RTMP+HLS with ssl.
SRS_RTMP_HLS=NO

#####################################################################################
# menu
#####################################################################################
function show_help() {
    cat << END

Options:
  -h, --help                print this message
                          
  --with-ssl                enable rtmp complex handshake, requires openssl-devel installed.
                            to delivery h264 video and aac audio to flash player.
  --with-hls                enable hls streaming, mux RTMP to m3u8/ts files.
  --with-dvr                enable dvr, mux RTMP to flv files.
  --with-nginx              enable delivery HTTP stream with nginx.
                            build nginx at: ./objs/nginx/sbin/nginx
  --with-http-callback      enable http hooks, build cherrypy as demo api server.
  --with-http-server        enable http server to delivery http stream.
  --with-http-api           enable http api, to manage SRS by http api.
  --with-ffmpeg             enable transcoding tool ffmpeg.
                            build ffmpeg at: ./objs/ffmpeg/bin/ffmpeg
  --with-transcode          enable transcoding features.
                            user must specifies the transcode tools in conf.
  --with-ingest             enable ingest features.
                            user must specifies the ingest tools in conf.
  --with-librtmp            enable srs-librtmp, library for client.
  --with-bwtc               enable SRS bandwidth test client tool.
  --with-research           build the research tools.
  --with-utest              build the utest for SRS.
  --with-gperf              build SRS with gperf tools(no gmc/gmp/gcp, with tcmalloc only).
  --with-gmc                build memory check for SRS with gperf tools.
  --with-gmp                build memory profile for SRS with gperf tools.
  --with-gcp                build cpu profile for SRS with gperf tools.
  --with-gprof              build SRS with gprof(GNU profile tool).
  --with-arm-ubuntu12       build SRS on ubuntu12 for armhf(v7cpu).
                          
  --without-ssl             disable rtmp complex handshake.
  --without-hls             disable hls, rtmp streaming only.
  --without-dvr             disable dvr, donot support record RTMP stream to flv.
  --without-nginx           disable delivery HTTP stream with nginx.
  --without-http-callback   disable http, http hooks callback.
  --without-http-server     disable http server, use external server to delivery http stream.
  --without-http-api        disable http api, only use console to manage SRS process.
  --without-ffmpeg          disable the ffmpeg transcode tool feature.
  --without-transcode       disable the transcoding feature.
  --without-ingest          disable the ingest feature.
  --without-librtmp         disable srs-librtmp, library for client.
  --without-bwtc            disable SRS bandwidth test client tool.
  --without-research        do not build the research tools.
  --without-utest           do not build the utest for SRS.
  --without-gperf           do not build SRS with gperf tools(without tcmalloc and gmc/gmp/gcp).
  --without-gmc             do not build memory check for SRS with gperf tools.
  --without-gmp             do not build memory profile for SRS with gperf tools.
  --without-gcp             do not build cpu profile for SRS with gperf tools.
  --without-gprof           do not build srs with gprof(GNU profile tool).
  --without-arm-ubuntu12    do not build srs on ubuntu12 for armhf(v7cpu).
                          
  --prefix=<path>           the absolute install path for srs.
  --static                  whether add '-static' to link options. always set this option for arm.
  --jobs[=N]                Allow N jobs at once; infinite jobs with no arg.
                            used for make in the configure, for example, to make ffmpeg.

Presets:
  --x86-x64                 [default] for x86/x64 cpu, common pc and servers.
  --pi                      for raspberry-pi(directly build), open features hls/ssl/static.
  --arm                     alias for --with-arm-ubuntu12
  --dev                     for dev, open all features, no gperf/gprof/arm.
  --fast                    the most fast compile, nothing, only support vp6 RTMP.
  --pure-rtmp               only support RTMP with ssl.
  --rtmp-hls                only support RTMP+HLS with ssl.
  
Conflicts:
  1. --dev/x86-x64 vs --static: 
        the --static only for arm cpus.
  2. --with-gmc vs --with-gmp: 
        @see: http://google-perftools.googlecode.com/svn/trunk/doc/heap_checker.html
  3. --with-gperf/gmc/gmp vs --with-gprof:
        gperftools not compatible with gprof.
  4. --arm vs --with-ffmpeg/bwtc/gperf/gmc/gmp/gprof:
        the complex tools not available for arm.

Workflow:
  1. apply "Presets". if not specified, use default preset.
  2. apply "Options". user specified option will override the preset.
  3. check conflicts. @see Conflicts section.
  4. generate detail features.

END
}

function parse_user_option() {
    case "$option" in
        -h)                             help=yes                    ;;
        --help)                         help=yes                    ;;
        
        --with-ssl)                     SRS_SSL=YES                 ;;
        --with-hls)                     SRS_HLS=YES                 ;;
        --with-dvr)                     SRS_DVR=YES                 ;;
        --with-nginx)                   SRS_NGINX=YES               ;;
        --with-ffmpeg)                  SRS_FFMPEG_TOOL=YES              ;;
        --with-transcode)               SRS_TRANSCODE=YES           ;;
        --with-ingest)                  SRS_INGEST=YES              ;;
        --with-http-callback)           SRS_HTTP_CALLBACK=YES       ;;
        --with-http-server)             SRS_HTTP_SERVER=YES         ;;
        --with-http-api)                SRS_HTTP_API=YES            ;;
        --with-librtmp)                 SRS_LIBRTMP=YES             ;;
        --with-bwtc)                    SRS_BWTC=YES                ;;
        --with-research)                SRS_RESEARCH=YES            ;;
        --with-utest)                   SRS_UTEST=YES               ;;
        --with-gperf)                   SRS_GPERF=YES               ;;
        --with-gmc)                     SRS_GPERF_MC=YES            ;;
        --with-gmp)                     SRS_GPERF_MP=YES            ;;
        --with-gcp)                     SRS_GPERF_CP=YES            ;;
        --with-gprof)                   SRS_GPROF=YES               ;;
        --with-arm-ubuntu12)            SRS_ARM_UBUNTU12=YES        ;;
                                                                 
        --without-ssl)                  SRS_SSL=NO                  ;;
        --without-hls)                  SRS_HLS=NO                  ;;
        --without-dvr)                  SRS_DVR=NO                  ;;
        --without-nginx)                SRS_NGINX=NO                ;;
        --without-ffmpeg)               SRS_FFMPEG_TOOL=NO               ;;
        --without-transcode)            SRS_TRANSCODE=NO            ;;
        --without-ingest)               SRS_INGEST=NO               ;;
        --without-http-callback)        SRS_HTTP_CALLBACK=NO        ;;
        --without-http-server)          SRS_HTTP_SERVER=NO          ;;
        --without-http-api)             SRS_HTTP_API=NO             ;;
        --without-librtmp)              SRS_LIBRTMP=NO              ;;
        --without-bwtc)                 SRS_BWTC=NO                 ;;
        --without-research)             SRS_RESEARCH=NO             ;;
        --without-utest)                SRS_UTEST=NO                ;;
        --without-gperf)                SRS_GPERF=NO                ;;
        --without-gmc)                  SRS_GPERF_MC=NO             ;;
        --without-gmp)                  SRS_GPERF_MP=NO             ;;
        --without-gcp)                  SRS_GPERF_CP=NO             ;;
        --without-gprof)                SRS_GPROF=NO                ;;
        --without-arm-ubuntu12)         SRS_ARM_UBUNTU12=NO         ;;
        
        --jobs)                         SRS_JOBS=${value}           ;;
        --prefix)                       SRS_PREFIX=${value}         ;;
        --static)                       SRS_STATIC=YES              ;;
        
        --x86-x64)                      SRS_X86_X64=YES             ;;
        --arm)                          SRS_ARM_UBUNTU12=YES        ;;
        --pi)                           SRS_PI=YES                  ;;
        --dev)                          SRS_DEV=YES                 ;;
        --fast)                         SRS_FAST=YES                ;;
        --pure-rtmp)                    SRS_PURE_RTMP=YES           ;;
        --rtmp-hls)                     SRS_RTMP_HLS=YES            ;;

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
            option=`echo "$option" | sed -e 's|=[-_a-zA-Z0-9/]*||'`
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
    # set default preset if not specifies
    if [ $SRS_RTMP_HLS = NO ]; then
        if [ $SRS_PURE_RTMP = NO ]; then
            if [ $SRS_FAST = NO ]; then
                if [ $SRS_DEV = NO ]; then
                    if [ $SRS_ARM_UBUNTU12 = NO ]; then
                        if [ $SRS_PI = NO ]; then
                            if [ $SRS_X86_X64 = NO ]; then
                                SRS_X86_X64=YES; opt="--x86-x64 $opt";
                            fi
                        fi
                    fi
                fi
            fi
        fi
    fi

    # all disabled.
    if [ $SRS_FAST = YES ]; then
        SRS_HLS=NO
        SRS_DVR=NO
        SRS_NGINX=NO
        SRS_SSL=NO
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=NO
        SRS_INGEST=NO
        SRS_HTTP_PARSER=NO
        SRS_HTTP_CALLBACK=NO
        SRS_HTTP_SERVER=NO
        SRS_HTTP_API=NO
        SRS_LIBRTMP=NO
        SRS_BWTC=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    # all disabled.
    if [ $SRS_RTMP_HLS = YES ]; then
        SRS_HLS=YES
        SRS_DVR=NO
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=NO
        SRS_INGEST=NO
        SRS_HTTP_PARSER=NO
        SRS_HTTP_CALLBACK=NO
        SRS_HTTP_SERVER=NO
        SRS_HTTP_API=NO
        SRS_LIBRTMP=NO
        SRS_BWTC=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    # only ssl for RTMP with complex handshake.
    if [ $SRS_PURE_RTMP = YES ]; then
        SRS_HLS=NO
        SRS_DVR=NO
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=NO
        SRS_INGEST=NO
        SRS_HTTP_PARSER=NO
        SRS_HTTP_CALLBACK=NO
        SRS_HTTP_SERVER=NO
        SRS_HTTP_API=NO
        SRS_LIBRTMP=NO
        SRS_BWTC=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    # if arm specified, set some default to disabled.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        SRS_HLS=YES
        SRS_DVR=YES
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=YES
        SRS_INGEST=YES
        SRS_HTTP_PARSER=YES
        SRS_HTTP_CALLBACK=YES
        SRS_HTTP_SERVER=YES
        SRS_HTTP_API=YES
        SRS_LIBRTMP=NO
        SRS_BWTC=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=YES
    fi

    # defaults for x86/x64
    if [ $SRS_X86_X64 = YES ]; then
        SRS_HLS=YES
        SRS_DVR=YES
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=YES
        SRS_INGEST=YES
        SRS_HTTP_PARSER=YES
        SRS_HTTP_CALLBACK=YES
        SRS_HTTP_SERVER=YES
        SRS_HTTP_API=YES
        SRS_LIBRTMP=YES
        SRS_BWTC=YES
        SRS_RESEARCH=NO
        SRS_UTEST=YES
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    # if dev specified, open features if possible.
    if [ $SRS_DEV = YES ]; then
        SRS_HLS=YES
        SRS_DVR=YES
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=YES
        SRS_TRANSCODE=YES
        SRS_INGEST=YES
        SRS_HTTP_PARSER=YES
        SRS_HTTP_CALLBACK=YES
        SRS_HTTP_SERVER=YES
        SRS_HTTP_API=YES
        SRS_LIBRTMP=YES
        SRS_BWTC=YES
        SRS_RESEARCH=YES
        SRS_UTEST=YES
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=NO
    fi

    # if raspberry-pi specified, open ssl/hls/static features
    if [ $SRS_PI = YES ]; then
        SRS_HLS=YES
        SRS_DVR=YES
        SRS_NGINX=NO
        SRS_SSL=YES
        SRS_FFMPEG_TOOL=NO
        SRS_TRANSCODE=YES
        SRS_INGEST=YES
        SRS_HTTP_PARSER=YES
        SRS_HTTP_CALLBACK=YES
        SRS_HTTP_SERVER=YES
        SRS_HTTP_API=YES
        SRS_LIBRTMP=NO
        SRS_BWTC=NO
        SRS_RESEARCH=NO
        SRS_UTEST=NO
        SRS_GPERF=NO
        SRS_GPERF_MC=NO
        SRS_GPERF_MP=NO
        SRS_GPERF_CP=NO
        SRS_GPROF=NO
        SRS_STATIC=YES
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

    # if http-xxxx specified, open the SRS_HTTP_PARSER
    SRS_HTTP_PARSER=NO
    if [ $SRS_HTTP_CALLBACK = YES ]; then SRS_HTTP_PARSER=YES; fi
    if [ $SRS_HTTP_SERVER = YES ]; then SRS_HTTP_PARSER=YES; fi
    if [ $SRS_HTTP_API = YES ]; then SRS_HTTP_PARSER=YES; fi

    # parse the jobs for make
    if [[ "" -eq SRS_JOBS ]]; then 
        export SRS_JOBS="--jobs=1" 
    else
        export SRS_JOBS="--jobs=${SRS_JOBS}"
    fi
}
apply_user_detail_options

function regenerate_options() {
    # save all config options to macro to write to auto headers file
    SRS_AUTO_USER_CONFIGURE="$opt"
    # regenerate the options for default values.
    SRS_AUTO_CONFIGURE="--prefix=${SRS_PREFIX}"
    if [ $SRS_HLS = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-hls"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-hls"; fi
    if [ $SRS_DVR = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-dvr"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-dvr"; fi
    if [ $SRS_NGINX = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-nginx"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-nginx"; fi
    if [ $SRS_SSL = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-ssl"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-ssl"; fi
    if [ $SRS_FFMPEG_TOOL = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-ffmpeg"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-ffmpeg"; fi
    if [ $SRS_TRANSCODE = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-transcode"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-transcode"; fi
    if [ $SRS_INGEST = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-ingest"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-ingest"; fi
    if [ $SRS_HTTP_CALLBACK = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-callback"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-callback"; fi
    if [ $SRS_HTTP_SERVER = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-server"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-server"; fi
    if [ $SRS_HTTP_API = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-http-api"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-http-api"; fi
    if [ $SRS_LIBRTMP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-librtmp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-librtmp"; fi
    if [ $SRS_BWTC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-bwtc"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-bwtc"; fi
    if [ $SRS_RESEARCH = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-research"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-research"; fi
    if [ $SRS_UTEST = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-utest"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-utest"; fi
    if [ $SRS_GPERF = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gperf"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gperf"; fi
    if [ $SRS_GPERF_MC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gmc"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gmc"; fi
    if [ $SRS_GPERF_MP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gmp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gmp"; fi
    if [ $SRS_GPERF_CP = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gcp"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gcp"; fi
    if [ $SRS_GPROF = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-gprof"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-gprof"; fi
    if [ $SRS_ARM_UBUNTU12 = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --with-arm-ubuntu12"; else SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --without-arm-ubuntu12"; fi
    if [ $SRS_STATIC = YES ]; then SRS_AUTO_CONFIGURE="${SRS_AUTO_CONFIGURE} --static"; fi
    echo "regenerate config: ${SRS_AUTO_CONFIGURE}"
}
regenerate_options

#####################################################################################
# check user options
#####################################################################################
function check_option_conflicts() {
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
        if [ $SRS_FFMPEG_TOOL = YES ]; then echo "ffmpeg for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_BWTC = YES ]; then echo "bwtc for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_RESEARCH = YES ]; then echo "research for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF = YES ]; then echo "gperf for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_MC = YES ]; then echo "gmc for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_MP = YES ]; then echo "gmp for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPERF_CP = YES ]; then echo "gcp for arm is not available, see: ./configure --help"; __check_ok=NO; fi
        if [ $SRS_GPROF = YES ]; then echo "gprof for arm is not available, see: ./configure --help"; __check_ok=NO; fi
    fi

    # if x86/x64 or directly build, never use static
    if [ $SRS_ARM_UBUNTU12 = NO ]; then
        if [ $SRS_PI = NO ]; then
            if [ $SRS_STATIC = YES ]; then
                echo "x86/x64 should never use static, see: ./configure --help"; __check_ok=NO;
            fi
        fi
    fi

    # check variable neccessary
    if [ $SRS_HLS = RESERVED ]; then echo "you must specifies the hls, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_DVR = RESERVED ]; then echo "you must specifies the dvr, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_NGINX = RESERVED ]; then echo "you must specifies the nginx, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_SSL = RESERVED ]; then echo "you must specifies the ssl, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_FFMPEG_TOOL = RESERVED ]; then echo "you must specifies the ffmpeg, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_HTTP_CALLBACK = RESERVED ]; then echo "you must specifies the http-callback, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_HTTP_SERVER = RESERVED ]; then echo "you must specifies the http-server, see: ./configure --help"; __check_ok=NO; fi
    if [ $SRS_HTTP_API = RESERVED ]; then echo "you must specifies the http-api, see: ./configure --help"; __check_ok=NO; fi
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
}
check_option_conflicts
