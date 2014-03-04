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
SRS_HTTP=RESERVED
SRS_RESEARCH=RESERVED
SRS_UTEST=RESERVED
# arguments
SRS_JOBS=1

# TODO: remove the default to yes.
SRS_HLS=YES
SRS_SSL=YES
SRS_FFMPEG=YES
SRS_HTTP=YES
SRS_RESEARCH=NO
SRS_UTEST=NO

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
        --with-http)                    SRS_HTTP=YES              ;;
        --with-research)                SRS_RESEARCH=YES          ;;
        --with-utest)                   SRS_UTEST=YES             ;;
        
        --without-ssl)                  SRS_SSL=NO                ;;
        --without-hls)                  SRS_HLS=NO                ;;
        --without-ffmpeg)               SRS_FFMPEG=NO             ;;
        --without-http)                 SRS_HTTP=NO               ;;
        --without-research)             SRS_RESEARCH=NO           ;;
        --without-utest)                SRS_UTEST=NO              ;;
        
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
  --with-http              enable http hooks, build cherrypy as demo api server.
                           srs will call the http hooks, such as: on_connect.
  --with-ffmpeg            enable transcoding with ffmpeg.
  --with-research          build the research tools.
  --with-utest             build the utest for srs.

  --without-ssl            disable rtmp complex handshake.
  --without-hls            disable hls, rtmp streaming only.
  --without-http           disable http, http hooks callback.
  --without-ffmpeg         disable the ffmpeg transcoding feature.
  --without-research       do not build the research tools.
  --without-utest          do not build the utest for srs.
  
  --jobs[=N]               Allow N jobs at once; infinite jobs with no arg.
                           used for make in the configure, for example, to make ffmpeg.

END
    exit 0
fi

#####################################################################################
# check user options
#####################################################################################
__check_ok=YES
if [ $SRS_SSL = RESERVED ]; then
    echo "you must specifies the ssl, see: ./configure --help";
    __check_ok=NO
fi
if [ $SRS_HLS = RESERVED ]; then
    echo "you must specifies the hls, see: ./configure --help";
    __check_ok=NO
fi
if [ $SRS_FFMPEG = RESERVED ]; then
    echo "you must specifies the ffmpeg, see: ./configure --help";
    __check_ok=NO
fi
if [ $SRS_HTTP = RESERVED ]; then
    echo "you must specifies the http, see: ./configure --help";
    __check_ok=NO
fi
if [ $SRS_RESEARCH = RESERVED ]; then
    echo "you must specifies the research, see: ./configure --help";
    __check_ok=NO
fi
if [ $SRS_UTEST = RESERVED ]; then
    echo "you must specifies the utest, see: ./configure --help";
    __check_ok=NO
fi
if [ $__check_ok = NO ]; then
    exit 1;
fi
