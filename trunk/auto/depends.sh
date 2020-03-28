#!/bin/bash

# variables, parent script must set it:
# SRS_JOBS: the build jobs.
# SrsArmMakeOptions: the arm make options for ubuntu12(armhf, v7cpu)

#####################################################################################
#####################################################################################
# prepare the depends tools and libraries
# DEPENDS: options.sh, only when user options parsed, the depends tools are known.
#####################################################################################
#####################################################################################

#####################################################################################
# utilities
#####################################################################################
function require_sudoer()
{
    sudo echo "" >/dev/null 2>&1
    
    ret=$?; if [[ 0 -ne $ret ]]; then 
        echo "\"$1\" require sudoer failed. ret=$ret";
        exit $ret; 
    fi
}

# TODO: check gcc/g++
echo "Checking gcc/g++/gdb/make."
echo "Required tools are ok."
#####################################################################################
# for Ubuntu, auto install tools by apt-get
#####################################################################################
OS_IS_UBUNTU=NO
function Ubuntu_prepare()
{
    if [ $SRS_CUBIE = YES ]; then
        echo "For cubieboard, please use ubuntu prepare."
    else
        uname -v|grep Ubuntu >/dev/null 2>&1
        ret=$?; if [[ 0 -ne $ret ]]; then
            # for debian, we think it's ubuntu also.
            # for example, the wheezy/sid which is debian armv7 linux, can not identified by uname -v.
            if [[ ! -f /etc/debian_version ]]; then
                return 0;
            fi
        fi
    fi

    OS_IS_UBUNTU=YES
    echo "Installing tools for Ubuntu."
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc."
        require_sudoer "sudo apt-get install -y --force-yes gcc"
        sudo apt-get install -y --force-yes gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc is installed."
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing g++."
        require_sudoer "sudo apt-get install -y --force-yes g++"
        sudo apt-get install -y --force-yes g++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The g++ is installed."
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing make."
        require_sudoer "sudo apt-get install -y --force-yes make"
        sudo apt-get install -y --force-yes make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The make is installed."
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing patch."
        require_sudoer "sudo apt-get install -y --force-yes patch"
        sudo apt-get install -y --force-yes patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The patch is installed."
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing unzip."
        require_sudoer "sudo apt-get install -y --force-yes unzip"
        sudo apt-get install -y --force-yes unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The unzip is installed."
    fi

    if [[ $SRS_NASM == YES ]]; then
      nasm -v >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
          echo "Installing nasm."
          require_sudoer "sudo apt-get install -y --force-yes nasm"
          sudo apt-get install -y --force-yes nasm; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
          echo "The nasm is installed."
      fi
    fi

    if [[ $SRS_VALGRIND == YES ]]; then
        valgrind --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing valgrind."
            require_sudoer "sudo apt-get install -y --force-yes valgrind"
            sudo apt-get install -y --force-yes valgrind; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind is installed."
        fi
    fi

    if [[ $SRS_VALGRIND == YES ]]; then
        if [[ ! -f /usr/include/valgrind/valgrind.h ]]; then
            echo "Installing valgrind-dev."
            require_sudoer "sudo apt-get install -y --force-yes valgrind-dbg"
            sudo apt-get install -y --force-yes valgrind-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind-dev is installed."
        fi
    fi
    
    echo "Tools for Ubuntu are installed."
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    Ubuntu_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Install tools for ubuntu failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# for Centos, auto install tools by yum
#####################################################################################
OS_IS_CENTOS=NO
function Centos_prepare()
{
    if [[ ! -f /etc/redhat-release ]]; then
        return 0;
    fi

    OS_IS_CENTOS=YES
    echo "Installing tools for Centos."
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc."
        require_sudoer "sudo yum install -y gcc"
        sudo yum install -y gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc is installed."
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc-c++."
        require_sudoer "sudo yum install -y gcc-c++"
        sudo yum install -y gcc-c++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc-c++ is installed."
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing make."
        require_sudoer "sudo yum install -y make"
        sudo yum install -y make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The make is installed."
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing patch."
        require_sudoer "sudo yum install -y patch"
        sudo yum install -y patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The patch is installed."
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing unzip."
        require_sudoer "sudo yum install -y unzip"
        sudo yum install -y unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The unzip is installed."
    fi

    nasm -v >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing nasm."
        require_sudoer "sudo yum install -y nasm"
        sudo yum install -y nasm; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The nasm is installed."
    fi

    if [[ $SRS_VALGRIND == YES ]]; then
        valgrind --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing valgrind."
            require_sudoer "sudo yum install -y valgrind"
            sudo yum install -y valgrind; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind is installed."
        fi
    fi

    if [[ $SRS_VALGRIND == YES ]]; then
        if [[ ! -f /usr/include/valgrind/valgrind.h ]]; then
            echo "Installing valgrind-devel."
            require_sudoer "sudo yum install -y valgrind-devel"
            sudo yum install -y valgrind-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The valgrind-devel is installed."
        fi
    fi
    
    echo "Tools for Centos are installed."
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    Centos_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Install tools for CentOS failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# For OSX, auto install tools by brew
#####################################################################################
OS_IS_OSX=NO
function OSX_prepare()
{
    uname -s|grep Darwin >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        if [ $SRS_OSX = YES ]; then
            echo "OSX check failed, actual is `uname -s`"
            exit 1;
        fi
        return 0;
    fi

    # cross build for arm, install the cross build tool chain.
    if [ $SRS_CROSS_BUILD = YES ]; then
        echo "embeded(arm/mips) is invalid for OSX"
        return 1
    fi

    OS_IS_OSX=YES
    echo "OSX detected, install tools if needed"
    # requires the osx when os
    if [ $OS_IS_OSX = YES ]; then
        if [ $SRS_OSX = NO ]; then
            echo "OSX detected, must specifies the --osx"
            exit 1
        fi
    fi

    brew --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install brew"
        echo "ruby -e \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)\""
        ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install brew success"
    fi

    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc"
        echo "brew install gcc"
        brew install gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install gcc success"
    fi

    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc-c++"
        echo "brew install gcc-c++"
        brew install gcc-c++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install gcc-c++ success"
    fi

    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install make"
        echo "brew install make"
        brew install make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install make success"
    fi

    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install patch"
        echo "brew install patch"
        brew install patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install patch success"
    fi

    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install unzip"
        echo "brew install unzip"
        brew install unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install unzip success"
    fi

    echo "OSX install tools success"
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    OSX_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "OSX prepare failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# for Centos, auto install tools by yum
#####################################################################################
# We must use a bash function instead of variable.
function sed_utility() {
    if [ $OS_IS_OSX = YES ]; then
        sed -i '' "$@"
    else
        sed -i "$@"
    fi

    ret=$?; if [[ $ret -ne 0 ]]; then
        if [ $OS_IS_OSX = YES ]; then
            echo "sed -i '' \"$@\""
        else
            echo "sed -i \"$@\""
        fi
        return $ret
    fi
}
SED="sed_utility" && echo "SED is $SED"

#####################################################################################
# check the os.
#####################################################################################
# Only supports:
#       linux, centos/ubuntu as such,
#       cross build for embeded system, for example, mips or arm,
#       directly build on arm/mips, for example, pi or cubie,
#       export srs-librtmp
# others is invalid.
if [[ $OS_IS_UBUNTU = NO && $OS_IS_CENTOS = NO && $OS_IS_OSX = NO && $SRS_EXPORT_LIBRTMP_PROJECT = NO ]]; then
    if [[ $SRS_PI = NO && $SRS_CUBIE = NO && $SRS_CROSS_BUILD = NO ]]; then
        echo "Your OS `uname -s` is not supported."
        exit 1
    fi
fi

#####################################################################################
# state-threads
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # check the cross build flag file, if flag changed, need to rebuild the st.
    _ST_MAKE=linux-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_EPOLL" && _ST_LD=${SRS_TOOL_LD}
    if [[ $SRS_VALGRIND == YES ]]; then
        _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_VALGRIND"
    fi
    # for osx, use darwin for st, donot use epoll.
    if [[ $SRS_OSX == YES ]]; then
        _ST_MAKE=darwin-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_KQUEUE" && _ST_LD=${SRS_TOOL_CC}
    fi
    # Pass the global extra flags.
    if [[ $SRS_EXTRA_FLAGS != '' ]]; then
      _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS $SRS_EXTRA_FLAGS"
    fi
    # Patched ST from https://github.com/ossrs/state-threads/tree/srs
    if [[ -f ${SRS_OBJS}/st/libst.a ]]; then
        echo "The state-threads is ok.";
    else
        echo "Building state-threads.";
        (
            rm -rf ${SRS_OBJS}/st-srs && cd ${SRS_OBJS} &&
            ln -sf ../3rdparty/st-srs && cd st-srs &&
            make clean && make ${_ST_MAKE} EXTRA_CFLAGS="${_ST_EXTRA_CFLAGS}" \
                CC=${SRS_TOOL_CC} AR=${SRS_TOOL_AR} LD=${_ST_LD} RANDLIB=${SRS_TOOL_RANDLIB} &&
            cd .. && rm -f st && ln -sf st-srs/obj st
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build state-threads failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "Build state-threads static lib failed."; exit -1; fi
fi

#####################################################################################
# srtp
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # Patched ST from https://github.com/ossrs/state-threads/tree/srs
    if [[ -f ${SRS_OBJS}/srtp2/lib/libsrtp2.a ]]; then
        echo "The srtp2 is ok.";
    else
        echo "Building srtp2.";
        (
            rm -rf ${SRS_OBJS}/srtp2 && cd ${SRS_OBJS} &&
            unzip -q ../3rdparty/libsrtp-2.0.0.zip && cd libsrtp-2.0.0 && 
            ./configure --prefix=`pwd`/_release && make ${SRS_JOBS} && make install &&
            cd .. && rm -f srtp2 && ln -sf libsrtp-2.0.0/_release srtp2
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build srtp2 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/srtp2/lib/libsrtp2.a ]; then echo "Build srtp2 static lib failed."; exit -1; fi
fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
function write_nginx_html5()
{
    cat<<END > ${html_file}
<video autoplay controls autobuffer type="application/vnd.apple.mpegurl"
    src="${hls_stream}">
</video>
END
}
# create the nginx dir, for http-server if not build nginx
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    mkdir -p ${SRS_OBJS}/nginx
fi

# the demo dir.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # create forward dir
    mkdir -p ${SRS_OBJS}/nginx/html/live &&
    mkdir -p ${SRS_OBJS}/nginx/html/forward/live

    # generate default html pages for android.
    html_file=${SRS_OBJS}/nginx/html/live/demo.html && hls_stream=demo.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/live/livestream.html && hls_stream=livestream.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/live/livestream_ld.html && hls_stream=livestream_ld.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/live/livestream_sd.html && hls_stream=livestream_sd.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/forward/live/livestream.html && hls_stream=livestream.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/forward/live/livestream_ld.html && hls_stream=livestream_ld.m3u8 && write_nginx_html5
    html_file=${SRS_OBJS}/nginx/html/forward/live/livestream_sd.html && hls_stream=livestream_sd.m3u8 && write_nginx_html5

    # copy players to nginx html dir.
    rm -rf ${SRS_OBJS}/nginx/html/players &&
    ln -sf `pwd`/research/players ${SRS_OBJS}/nginx/html/players &&
    rm -f ${SRS_OBJS}/nginx/crossdomain.xml &&
    ln -sf `pwd`/research/players/crossdomain.xml ${SRS_OBJS}/nginx/html/crossdomain.xml

    # for favicon.ico
    rm -rf ${SRS_OBJS}/nginx/html/favicon.ico &&
    ln -sf `pwd`/research/api-server/static-dir/favicon.ico ${SRS_OBJS}/nginx/html/favicon.ico

    # nginx.html to detect whether nginx is alive
    echo "Nginx is ok." > ${SRS_OBJS}/nginx/html/nginx.html
fi

#####################################################################################
# cherrypy for http hooks callback, CherryPy-3.2.4
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    if [[ -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        echo "Installing CherryPy-3.2.4";
        (
            rm -rf ${SRS_OBJS}/CherryPy-3.2.4 && cd ${SRS_OBJS} &&
            unzip -q ../3rdparty/CherryPy-3.2.4.zip && cd CherryPy-3.2.4 && 
            python setup.py install --user
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build CherryPy-3.2.4 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]; then echo "build CherryPy-3.2.4 failed."; exit -1; fi

    echo "Link players to cherrypy static-dir"
    rm -rf research/api-server/static-dir/players &&
    ln -sf `pwd`/research/players research/api-server/static-dir/players &&
    rm -f research/api-server/static-dir/crossdomain.xml &&
    ln -sf `pwd`/research/players/crossdomain.xml research/api-server/static-dir/crossdomain.xml &&
    rm -rf research/api-server/static-dir/live && 
    mkdir -p `pwd`/${SRS_OBJS}/nginx/html/live &&
    ln -sf `pwd`/${SRS_OBJS}/nginx/html/live research/api-server/static-dir/live &&
    rm -rf research/api-server/static-dir/forward && 
    mkdir -p `pwd`/${SRS_OBJS}/nginx/html/forward &&
    ln -sf `pwd`/${SRS_OBJS}/nginx/html/forward research/api-server/static-dir/forward
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Warning: Ignore error to link players to cherrypy static-dir."; fi
fi

#####################################################################################
# openssl, for rtmp complex handshake and HLS encryption.
#####################################################################################
if [[ $SRS_SSL == YES && $SRS_USE_SYS_SSL == YES ]]; then
    echo "Warning: Use system libssl, without compiling openssl."
fi
# @see http://www.openssl.org/news/secadv/20140407.txt
# Affected users should upgrade to OpenSSL 1.1.0e. Users unable to immediately
# upgrade can alternatively recompile OpenSSL with -DOPENSSL_NO_HEARTBEATS.
if [[ $SRS_SSL == YES && $SRS_USE_SYS_SSL != YES ]]; then
    OPENSSL_OPTIONS="-no-shared -no-threads -no-asm -DOPENSSL_NO_HEARTBEATS"
    OPENSSL_CONFIG="./config"
    # https://stackoverflow.com/questions/15539062/cross-compiling-of-openssl-for-linux-arm-v5te-linux-gnueabi-toolchain
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        OPENSSL_CONFIG="./Configure linux-armv4"
    else
        # If not crossbuild, try to use exists libraries.
        if [[ -f /usr/local/lib64/libssl.a && ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
            (mkdir -p  ${SRS_OBJS}/openssl/lib && cd ${SRS_OBJS}/openssl/lib &&
                ln -sf /usr/local/lib64/libssl.a && ln -sf /usr/local/lib64/libcrypto.a)
            (mkdir -p ${SRS_OBJS}/openssl/include && cd ${SRS_OBJS}/openssl/include &&
                ln -sf /usr/local/include/openssl)
        fi
    fi
    # cross build not specified, if exists flag, need to rebuild for no-arm platform.
    if [[ -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
        echo "Openssl-1.1.0e is ok.";
    else
        echo "Building openssl-1.1.0e.";
        (
            rm -rf ${SRS_OBJS}/openssl-1.1.0e && cd ${SRS_OBJS} &&
            unzip -q ../3rdparty/openssl-1.1.0e.zip && cd openssl-1.1.0e &&
            ${OPENSSL_CONFIG} --prefix=`pwd`/_release $OPENSSL_OPTIONS &&
            make CC=${SRS_TOOL_CC} AR="${SRS_TOOL_AR} -rs" LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB} ${SRS_JOBS} && make install_sw &&
            cd .. && rm -rf openssl && ln -sf openssl-1.1.0e/_release openssl
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build openssl-1.1.0e failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "Build openssl-1.1.0e failed."; exit -1; fi
fi

#####################################################################################
# libopus, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_EXPORT_LIBRTMP_PROJECT == NO && $SRS_RTC == YES ]]; then
    if [[ -f ${SRS_OBJS}/opus/lib/libopus.a ]]; then
        echo "The opus-1.3.1 is ok.";
    else
        echo "Building opus-1.3.1.";
        (
            rm -rf ${SRS_OBJS}/opus-1.3.1 && cd ${SRS_OBJS} &&
            tar xf ../3rdparty/opus-1.3.1.tar.gz && cd opus-1.3.1 &&
            ./configure --prefix=`pwd`/_release --enable-static --disable-shared && make ${SRS_JOBS} && make install
            cd .. && rm -rf opus && ln -sf opus-1.3.1/_release opus
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build opus-1.3.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/opus/lib/libopus.a ]; then echo "Build opus-1.3.1 failed."; exit -1; fi
fi

#####################################################################################
# ffmpeg-fix, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_EXPORT_LIBRTMP_PROJECT == NO && $SRS_RTC == YES ]]; then
    FFMPEG_OPTIONS=""
    if [[ $SRS_NASM == NO ]]; then
        FFMPEG_OPTIONS="--disable-asm --disable-x86asm --disable-inline-asm"
    fi
    if [[ -f ${SRS_OBJS}/ffmpeg/lib/libavcodec.a ]]; then
        echo "The ffmpeg-4.2-fit is ok.";
    else
        echo "Building ffmpeg-4.2-fit.";
        (
            rm -rf ${SRS_OBJS}/ffmpeg-4.2-fit && cd ${SRS_OBJS} && ABS_OBJS=`pwd` &&
            ln -sf ../3rdparty/ffmpeg-4.2-fit && cd ffmpeg-4.2-fit &&
            PKG_CONFIG_PATH=$ABS_OBJS/opus/lib/pkgconfig ./configure \
              --prefix=`pwd`/_release \
              --pkg-config-flags="--static" --extra-libs=-lpthread --extra-libs=-lm ${FFMPEG_OPTIONS} \
              --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
              --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network \
              --disable-dct --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils \
              --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox --disable-appkit --disable-coreimage \
              --disable-avfoundation --disable-securetransport --disable-iconv --disable-lzma --disable-sdl2 --disable-everything \
              --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm --enable-decoder=libopus --enable-encoder=aac \
              --enable-encoder=opus --enable-encoder=libopus --enable-libopus &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -rf ffmpeg && ln -sf ffmpeg-4.2-fit/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-4.2-fit failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/ffmpeg/lib/libavcodec.a ]; then echo "Build ffmpeg-4.2-fit failed."; exit -1; fi
fi

#####################################################################################
# live transcoding, ffmpeg-4.1, x264-core157, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
# Always link the ffmpeg tools if exists.
if [[ -f /usr/local/bin/ffmpeg && ! -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
    mkdir -p ${SRS_OBJS}/ffmpeg/bin && ln -sf /usr/local/bin/ffmpeg ${SRS_OBJS}/ffmpeg/bin/ffmpeg
fi
if [ $SRS_FFMPEG_TOOL = YES ]; then
    if [[ -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
        echo "ffmpeg-4.1 is ok.";
    else
        echo "no ffmpeg-4.1 found, please run in docker ossrs/srs:dev";
        exit -1;
    fi
fi

#####################################################################################
# SRT module, https://github.com/ossrs/srs/issues/1147#issuecomment-577469119
#####################################################################################
if [[ $SRS_SRT == YES ]]; then
    if [[ -f /usr/local/lib64/libsrt.a && ! -f ${SRS_OBJS}/srt/lib/libsrt.a ]]; then
        mkdir -p ${SRS_OBJS}/srt/lib && ln -sf /usr/local/lib64/libsrt.a ${SRS_OBJS}/srt/lib/libsrt.a
        mkdir -p ${SRS_OBJS}/srt/include && ln -sf /usr/local/include/srt ${SRS_OBJS}/srt/include/
    fi
    if [[ -f ${SRS_OBJS}/srt/lib/libsrt.a ]]; then
        echo "libsrt-1.4.1 is ok.";
    else
        echo "no libsrt, please run in docker ossrs/srs:srt or build from source https://github.com/ossrs/srs/issues/1147#issuecomment-577469119";
        exit -1;
    fi
fi

#####################################################################################
# build research code, librtmp
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    if [ $SRS_RESEARCH = YES ]; then
        mkdir -p ${SRS_OBJS}/research

        (cd ${SRS_WORKDIR}/research/hls && make ${SRS_JOBS} && mv ts_info ../../${SRS_OBJS_DIR}/research)
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build research/hls failed, ret=$ret"; exit $ret; fi

        (cd research/ffempty && make ${SRS_JOBS} && mv ffempty ../../${SRS_OBJS_DIR}/research)
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build research/ffempty failed, ret=$ret"; exit $ret; fi
    fi
fi

if [[ $SRS_LIBRTMP == YES ]]; then
    mkdir -p ${SRS_OBJS}/research
    
    # librtmp
    (cd ${SRS_WORKDIR}/research/librtmp && mkdir -p objs &&
        rm -rf ../../${SRS_OBJS_DIR}/research/librtmp &&
        ln -sf `pwd`/objs ../../${SRS_OBJS_DIR}/research/librtmp)
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Link research/librtmp failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# build utest code
#####################################################################################
if [ $SRS_UTEST = YES ]; then
    if [[ -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]]; then
        echo "The gtest-1.6.0 is ok.";
    else
        echo "Build gtest-1.6.0";
        (
            rm -rf ${SRS_OBJS}/gtest-1.6.0 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/gtest-1.6.0.zip &&
            rm -rf gtest && ln -sf gtest-1.6.0 gtest
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gtest-1.6.0 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]; then echo "Build gtest-1.6.0 failed."; exit -1; fi
fi

#####################################################################################
# build gperf code
#####################################################################################
if [ $SRS_GPERF = YES ]; then
    if [[ -f ${SRS_OBJS}/gperf/bin/pprof ]]; then
        echo "The gperftools-2.1 is ok.";
    else
        echo "Build gperftools-2.1";
        (
            rm -rf ${SRS_OBJS}/gperftools-2.1 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/gperftools-2.1.zip && cd gperftools-2.1 &&
            ./configure --prefix=`pwd`/_release --enable-frame-pointers && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf gperf && ln -sf gperftools-2.1/_release gperf &&
            rm -rf pprof && ln -sf gperf/bin/pprof pprof
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gperftools-2.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/gperf/bin/pprof ]; then echo "Build gperftools-2.1 failed."; exit -1; fi
fi

#####################################################################################
# generated the test script
#####################################################################################
rm -rf ${SRS_OBJS}/srs.test && ln -sf `pwd`/scripts/srs.test objs/srs.test
