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

    pkg-config --version >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing pkg-config."
        require_sudoer "sudo apt-get install -y --force-yes pkg-config"
        sudo apt-get install -y --force-yes pkg-config; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The pkg-config is installed."
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

    pkg-config --version --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Please install pkg-config"; exit -1;
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
    # requires the osx when os
    if [ $OS_IS_OSX = YES ]; then
        if [ $SRS_OSX = NO ]; then
            echo "OSX detected, please use: ./configure --osx"
            exit 1
        fi
    fi

    echo "OSX detected, install tools if needed"

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

    pkg-config --version >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Please install pkg-config"; exit -1;
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

function _srs_link_file()
{
    tmp_dir=$1; if [[ $tmp_dir != *'/' ]]; then tmp_dir+='/'; fi
    tmp_dest=$2; if [[ $tmp_dest != *'/' ]]; then tmp_dest+='/'; fi
    tmp_prefix=$3; if [[ $tmp_prefix != *'/' ]]; then tmp_prefix+='/'; fi

    echo "LINK files at dir: $tmp_dir, dest: $tmp_dest, prefix: $tmp_prefix, pwd: `pwd`"
    for file in `(cd $tmp_dir && find . -maxdepth 1 -type f ! -name '*.o' ! -name '*.d' ! -name '*.log')`; do
        basefile=`basename $file` &&
        #echo "ln -sf ${tmp_prefix}${tmp_dir}$basefile ${tmp_dest}$basefile" &&
        ln -sf ${tmp_prefix}${tmp_dir}$basefile ${tmp_dest}$basefile;
    done
}

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
    _ST_MAKE=linux-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_EPOLL" && _ST_LD=${SRS_TOOL_LD} && _ST_OBJ="LINUX_*"
    if [[ $SRS_VALGRIND == YES ]]; then
        _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_VALGRIND"
    fi
    # for osx, use darwin for st, donot use epoll.
    if [[ $SRS_OSX == YES ]]; then
        _ST_MAKE=darwin-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_KQUEUE" && _ST_LD=${SRS_TOOL_CC} && _ST_OBJ="DARWIN_*"
    fi
    # For UDP sendmmsg, disable it if not suppported.
    if [[ $SRS_SENDMMSG == YES ]]; then
        echo "Build ST with UDP sendmmsg support."
        _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_HAVE_SENDMMSG -D_GNU_SOURCE"
    fi
    # Pass the global extra flags.
    if [[ $SRS_EXTRA_FLAGS != '' ]]; then
        _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS $SRS_EXTRA_FLAGS"
    fi
    # Patched ST from https://github.com/ossrs/state-threads/tree/srs
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/st/libst.a ]]; then
        echo "The state-threads is ok.";
    else
        echo "Building state-threads.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/st-srs && mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/st-srs &&
            # Create a hidden directory .src
            cd ${SRS_OBJS}/${SRS_PLATFORM}/st-srs && ln -sf ../../../3rdparty/st-srs .src &&
            # Link source files under .src
            _srs_link_file .src/ ./ ./ &&
            for dir in `(cd .src && find . -maxdepth 1 -type d|grep '\./')`; do
                dir=`basename $dir` && mkdir -p $dir && _srs_link_file .src/$dir/ $dir/ ../
            done &&
            # Link source files under .src/xxx, the first child dir.
            for dir in `(cd .src && find . -maxdepth 1 -type d|grep '\./'|grep -v Linux|grep -v Darwin)`; do
                mkdir -p $dir &&
                for file in `(cd .src/$dir && find . -maxdepth 1 -type f ! -name '*.o' ! -name '*.d')`; do
                    ln -sf ../.src/$dir/$file $dir/$file;
                done;
            done &&
            # Build source code.
            make ${_ST_MAKE} EXTRA_CFLAGS="${_ST_EXTRA_CFLAGS}" \
                CC=${SRS_TOOL_CC} AR=${SRS_TOOL_AR} LD=${_ST_LD} RANDLIB=${SRS_TOOL_RANDLIB} &&
            cd .. && rm -f st && ln -sf st-srs/${_ST_OBJ} st
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build state-threads failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf st && ln -sf ${SRS_PLATFORM}/st-srs/${_ST_OBJ} st)
    if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "Build state-threads static lib failed."; exit -1; fi
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
    # Detect python or python2
    python --version >/dev/null 2>&1 && SYS_PYTHON=python;
    python2 --version >/dev/null 2>&1 && SYS_PYTHON=python2;
    # Install cherrypy for api server.
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        echo "Installing CherryPy-3.2.4";
        (
            rm -rf ${SRS_OBJS}/CherryPy-3.2.4 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            unzip -q ../../3rdparty/CherryPy-3.2.4.zip && cd CherryPy-3.2.4 &&
            $SYS_PYTHON setup.py install --user --prefix=''
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build CherryPy-3.2.4 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/${SRS_PLATFORM}/CherryPy-3.2.4/setup.py ]; then echo "build CherryPy-3.2.4 failed."; exit -1; fi

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
    OPENSSL_OPTIONS="-no-shared -no-threads -DOPENSSL_NO_HEARTBEATS"
    OPENSSL_CONFIG="./config"
    # https://stackoverflow.com/questions/15539062/cross-compiling-of-openssl-for-linux-arm-v5te-linux-gnueabi-toolchain
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        OPENSSL_CONFIG="./Configure linux-armv4"
    elif [[ ! -f ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib/libssl.a ]]; then
        # For older docker, which does not support SRTP asm optimization.
        if [[ -f /usr/local/lib64/libssl.a ]]; then
            (mkdir -p  ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib &&
                ln -sf /usr/local/lib64/libssl.a && ln -sf /usr/local/lib64/libcrypto.a &&
                mkdir -p /usr/local/lib64/pkgconfig && ln -sf /usr/local/lib64/pkgconfig)
            (mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include &&
                ln -sf /usr/local/include/openssl)
        fi
        # Try to use files for openssl 1.0.*
        if [[ -f /usr/local/ssl/lib/libssl.a ]]; then
            (mkdir -p  ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib &&
                ln -sf /usr/local/ssl/lib/libssl.a && ln -sf /usr/local/ssl/lib/libcrypto.a &&
                mkdir -p /usr/local/ssl/lib/pkgconfig && ln -sf /usr/local/ssl/lib/pkgconfig)
            (mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include &&
                ln -sf /usr/local/ssl/include/openssl)
        fi
    fi
    # For RTC, we should use ASM to improve performance, not a little improving.
    if [[ $SRS_RTC == NO || $SRS_NASM == NO ]]; then
        OPENSSL_OPTIONS="$OPENSSL_OPTIONS -no-asm"
    fi
    # Mac OS X can have issues (its often a neglected platform).
    # @see https://wiki.openssl.org/index.php/Compilation_and_Installation
    if [[ $SRS_OSX == YES ]]; then
        export KERNEL_BITS=64;
    fi
    # Which openssl we choose, openssl-1.0.* for SRTP with ASM, others we use openssl-1.1.*
    OPENSSL_CANDIDATE="openssl-1.1.0e" && OPENSSL_UNZIP="unzip -q ../../3rdparty/$OPENSSL_CANDIDATE.zip"
    if [[ $SRS_SRTP_ASM == YES ]]; then
        OPENSSL_CANDIDATE="openssl-OpenSSL_1_0_2u" && OPENSSL_UNZIP="tar xf  ../../3rdparty/$OPENSSL_CANDIDATE.tar.gz"
    fi
    # cross build not specified, if exists flag, need to rebuild for no-arm platform.
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib/libssl.a ]]; then
        echo "Openssl-1.1.0e is ok.";
    else
        echo "Building $OPENSSL_CANDIDATE.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            ${OPENSSL_UNZIP} && cd $OPENSSL_CANDIDATE && ${OPENSSL_CONFIG} --prefix=`pwd`/_release $OPENSSL_OPTIONS &&
            make CC=${SRS_TOOL_CC} AR="${SRS_TOOL_AR} -rs" LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB} ${SRS_JOBS} && make install_sw &&
            cd .. && rm -rf openssl && ln -sf $OPENSSL_CANDIDATE/_release openssl
        )
    fi
    # Which lib we use.
    OPENSSL_LIB="$OPENSSL_CANDIDATE/_release"
    if [[ ! -f ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_LIB}/lib/libssl.a ]]; then
        OPENSSL_LIB="openssl"
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build $OPENSSL_CANDIDATE failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf openssl && ln -sf ${SRS_PLATFORM}/${OPENSSL_LIB} openssl)
    if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "Build $OPENSSL_CANDIDATE failed."; exit -1; fi
fi

#####################################################################################
# srtp
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # For openssl-1.1.*, we should disable SRTP ASM, because SRTP only works with openssl-1.0.*
    if [[ $SRS_SRTP_ASM == YES ]]; then
        echo "  #include <openssl/ssl.h>                              " > ${SRS_OBJS}/_tmp_srtp_asm_detect.c
        echo "  #if OPENSSL_VERSION_NUMBER >= 0x10100000L // v1.1.x   " >> ${SRS_OBJS}/_tmp_srtp_asm_detect.c
        echo "  #error \"SRTP only works with openssl-1.0.*\"         " >> ${SRS_OBJS}/_tmp_srtp_asm_detect.c
        echo "  #endif                                                " >> ${SRS_OBJS}/_tmp_srtp_asm_detect.c
        ${SRS_TOOL_CC} -c ${SRS_OBJS}/_tmp_srtp_asm_detect.c -I${SRS_OBJS}/openssl/include -o /dev/null >/dev/null 2>&1
        if [[ $? -ne 0 ]]; then
            SRS_SRTP_ASM=NO && echo "Warning: Disable SRTP ASM optimization, please update docker";
        fi
        rm -f ${SRS_OBJS}/_tmp_srtp_asm_detect.c
    fi;
    SRTP_CONFIG="echo SRTP without openssl(ASM) optimization" && SRTP_OPTIONS=""
    # If use ASM for SRTP, we enable openssl(with ASM).
    if [[ $SRS_SRTP_ASM == YES ]]; then
        echo "SRTP with openssl(ASM) optimization" &&
        SRTP_CONFIG="export PKG_CONFIG_PATH=../openssl/lib/pkgconfig" && SRTP_OPTIONS="--enable-openssl"
    fi
    # Patched ST from https://github.com/ossrs/state-threads/tree/srs
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/srtp2/lib/libsrtp2.a ]]; then
        echo "The srtp2 is ok.";
    else
        echo "Building srtp2.";
        (
            rm -rf ${SRS_OBJS}/srtp2 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            rm -rf libsrtp-2.0.0 && unzip -q ../../3rdparty/libsrtp-2.0.0.zip && cd libsrtp-2.0.0 &&
            ${SRTP_CONFIG} && ./configure ${SRTP_OPTIONS} --prefix=`pwd`/_release &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -f srtp2 && ln -sf libsrtp-2.0.0/_release srtp2
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build srtp2 failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -f srtp2 && ln -sf ${SRS_PLATFORM}/libsrtp-2.0.0/_release srtp2)
    if [ ! -f ${SRS_OBJS}/srtp2/lib/libsrtp2.a ]; then echo "Build srtp2 static lib failed."; exit -1; fi
fi

#####################################################################################
# libopus, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_EXPORT_LIBRTMP_PROJECT == NO && $SRS_RTC == YES ]]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/opus/lib/libopus.a ]]; then
        echo "The opus-1.3.1 is ok.";
    else
        echo "Building opus-1.3.1.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            tar xf ../../3rdparty/opus-1.3.1.tar.gz && cd opus-1.3.1 &&
            ./configure --prefix=`pwd`/_release --enable-static --disable-shared && make ${SRS_JOBS} && make install
            cd .. && rm -rf opus && ln -sf opus-1.3.1/_release opus
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build opus-1.3.1 failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -f opus && ln -sf ${SRS_PLATFORM}/opus-1.3.1/_release opus)
    if [ ! -f ${SRS_OBJS}/opus/lib/libopus.a ]; then echo "Build opus-1.3.1 failed."; exit -1; fi
fi

#####################################################################################
# ffmpeg-fix, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_EXPORT_LIBRTMP_PROJECT == NO && $SRS_RTC == YES ]]; then
    FFMPEG_OPTIONS=""

    # If disable nasm, disable all ASMs.
    if [[ $SRS_NASM == NO ]]; then
        FFMPEG_OPTIONS="--disable-asm --disable-x86asm --disable-inline-asm"
    fi
    # If no nasm, we disable the x86asm.
    nasm -v >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        FFMPEG_OPTIONS="--disable-x86asm"
    fi

    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/lib/libavcodec.a ]]; then
        echo "The ffmpeg-4.2-fit is ok.";
    else
        echo "Building ffmpeg-4.2-fit.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4.2-fit && mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4.2-fit &&
            # Create a hidden directory .src
            cd ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4.2-fit && ABS_OBJS=`(cd .. && pwd)` && ln -sf ../../../3rdparty/ffmpeg-4.2-fit .src &&
            # Link source files under .src
            _srs_link_file .src/ ./ ./ &&
            for dir in `(cd .src && find . -maxdepth 1 -type d|grep '\./')`; do
                dir=`basename $dir` && mkdir -p $dir && _srs_link_file .src/$dir/ $dir/ ../ &&
                for dir2 in `(cd .src/$dir && find . -maxdepth 1 -type d|grep '\./')`; do
                    dir2=`basename $dir2` && mkdir -p $dir/$dir2 && _srs_link_file .src/$dir/$dir2/ $dir/$dir2/ ../../ &&
                    for dir3 in `(cd .src/$dir/$dir2 && find . -maxdepth 1 -type d|grep '\./')`; do
                        dir3=`basename $dir3` && mkdir -p $dir/$dir2/$dir3 && _srs_link_file .src/$dir/$dir2/$dir3/ $dir/$dir2/$dir3/ ../../../;
                    done
                done
            done &&
            # We should remove some files(in .gitignore) to keep them in local generated.
            (cd ffbuild && rm -f config.fate config.log config.mak config.sh .config) &&
            (cd libavutil && rm -f lib.version libavutil.version ffversion.h avconfig.h) &&
            (rm -rf doc && rm -f config.asm config.h libavcodec/libavcodec.version libswresample/libswresample.version) &&
            # Build source code.
            PKG_CONFIG_PATH=$ABS_OBJS/opus/lib/pkgconfig ./configure \
              --prefix=`pwd`/${SRS_PLATFORM}/_release \
              --pkg-config-flags="--static" --extra-libs=-lpthread --extra-libs=-lm ${FFMPEG_OPTIONS} \
              --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
              --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network \
              --disable-dct --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils \
              --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox --disable-cuda-llvm --disable-cuvid \
              --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-v4l2-m2m --disable-vaapi \
              --disable-vdpau --disable-appkit --disable-coreimage --disable-avfoundation --disable-securetransport --disable-iconv \
              --disable-lzma --disable-sdl2 --disable-everything --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm \
              --enable-decoder=libopus --enable-encoder=aac --enable-encoder=opus --enable-encoder=libopus --enable-libopus &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -rf ffmpeg && ln -sf ffmpeg-4.2-fit/${SRS_PLATFORM}/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-4.2-fit failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf ffmpeg && ln -sf ${SRS_PLATFORM}/ffmpeg-4.2-fit/${SRS_PLATFORM}/_release ffmpeg)
    if [ ! -f ${SRS_OBJS}/ffmpeg/lib/libavcodec.a ]; then echo "Build ffmpeg-4.2-fit failed."; exit -1; fi
fi

#####################################################################################
# live transcoding, ffmpeg-4.1, x264-core157, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
# Always link the ffmpeg tools if exists.
if [[ -f /usr/local/bin/ffmpeg && ! -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin/ffmpeg ]]; then
    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin &&
    ln -sf /usr/local/bin/ffmpeg ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin/ffmpeg
fi
if [ $SRS_FFMPEG_TOOL = YES ]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin/ffmpeg ]]; then
        echo "ffmpeg-4.1 is ok.";
    else
        echo "no ffmpeg found, please use srs-docker or --without-ffmpeg";
        exit -1;
    fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf ffmpeg && ln -sf ${SRS_PLATFORM}/ffmpeg)
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
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/gtest/include/gtest/gtest.h ]]; then
        echo "The gtest-1.6.0 is ok.";
    else
        echo "Build gtest-1.6.0";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/gtest-1.6.0 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            unzip -q ../../3rdparty/gtest-1.6.0.zip &&
            rm -rf gtest && ln -sf gtest-1.6.0 gtest
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gtest-1.6.0 failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf gtest && ln -sf ${SRS_PLATFORM}/gtest-1.6.0 gtest)
    if [ ! -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]; then echo "Build gtest-1.6.0 failed."; exit -1; fi
fi

#####################################################################################
# build gperf code
#####################################################################################
if [ $SRS_GPERF = YES ]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/gperf/bin/pprof ]]; then
        echo "The gperftools-2.1 is ok.";
    else
        echo "Build gperftools-2.1";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2.1 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            unzip -q ../../3rdparty/gperftools-2.1.zip && cd gperftools-2.1 &&
            ./configure --prefix=`pwd`/_release --enable-frame-pointers && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf gperf && ln -sf gperftools-2.1/_release gperf &&
            rm -rf pprof && ln -sf gperf/bin/pprof pprof
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gperftools-2.1 failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf pprof && ln -sf ${SRS_PLATFORM}/gperf/bin/pprof pprof)
    (cd ${SRS_OBJS} && rm -rf gperf && ln -sf ${SRS_PLATFORM}/gperftools-2.1/_release gperf)
    if [ ! -f ${SRS_OBJS}/pprof ]; then echo "Build gperftools-2.1 failed."; exit -1; fi
fi
