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
    uname -v|grep Ubuntu >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        # for debian, we think it's ubuntu also.
        # for example, the wheezy/sid which is debian armv7 linux, can not identified by uname -v.
        if [[ ! -f /etc/debian_version ]]; then
            return 0;
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

    if [[ $SRS_SRT == YES ]]; then
        echo "SRT enable, install depend tools"
        tclsh <<< "exit" >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing tcl."
            require_sudoer "sudo apt-get install -y --force-yes tcl"
            sudo apt-get install -y --force-yes tcl; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The tcl is installed."
        fi

        cmake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing cmake."
            require_sudoer "sudo apt-get install -y --force-yes cmake"
            sudo apt-get install -y --force-yes cmake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The cmake is installed."
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
Ubuntu_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Install tools for ubuntu failed, ret=$ret"; exit $ret; fi

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

    if [[ $SRS_SRT == YES ]]; then
        echo "SRT enable, install depend tools"
        tclsh <<< "exit" >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing tcl."
            require_sudoer "sudo yum install -y tcl"
            sudo yum install -y tcl; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The tcl is installed."
        fi

        cmake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing cmake."
            require_sudoer "sudo  yum install -y cmake"
            sudo yum install -y cmake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The cmake is installed."
        fi
    fi

    pkg-config --version --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Please install pkg-config"; exit -1;
    fi
    
    echo "Tools for Centos are installed."
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
Centos_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Install tools for CentOS failed, ret=$ret"; exit $ret; fi

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
        echo "The embeded(arm/mips) is invalid for OSX"
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

    if [[ $SRS_SRT == YES ]]; then
        echo "SRT enable, install depend tools"
        tclsh <<< "exit" >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing tcl."
            echo "brew install tcl."
            brew install tcl; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install tcl success"
        fi

        cmake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing cmake."
            echo "brew install cmake."
            brew install cmake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install cmake success"
        fi
    fi

    echo "OSX install tools success"
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
OSX_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "OSX prepare failed, ret=$ret"; exit $ret; fi

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
if [[ $OS_IS_UBUNTU = NO && $OS_IS_CENTOS = NO && $OS_IS_OSX = NO && $SRS_CROSS_BUILD = NO ]]; then
    echo "Your OS `uname -s` is not supported."
    exit 1
fi

#####################################################################################
# state-threads
#####################################################################################
# check the cross build flag file, if flag changed, need to rebuild the st.
_ST_MAKE=linux-debug && _ST_EXTRA_CFLAGS="-O0" && _ST_OBJ="LINUX_`uname -r`_DBG"
if [[ $SRS_VALGRIND == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_VALGRIND"
fi
# for osx, use darwin for st, donot use epoll.
if [[ $SRS_OSX == YES ]]; then
    _ST_MAKE=darwin-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_KQUEUE" && _ST_OBJ="DARWIN_`uname -r`_DBG"
fi
# For Ubuntu, the epoll detection might be fail.
if [[ $OS_IS_UBUNTU == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_HAVE_EPOLL"
fi
# Whether enable debug stats.
if [[ $SRS_DEBUG_STATS == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DDEBUG_STATS"
fi
# Always alloc on heap, @see https://github.com/ossrs/srs/issues/509#issuecomment-719931676
_ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMALLOC_STACK"
# Pass the global extra flags.
if [[ $SRS_EXTRA_FLAGS != '' ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS $SRS_EXTRA_FLAGS"
fi
# Patched ST from https://github.com/ossrs/state-threads/tree/srs
if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/st-srs/${_ST_OBJ}/libst.a ]]; then
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
            CC=${SRS_TOOL_CC} AR=${SRS_TOOL_AR} LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB} &&
        cd .. && rm -rf st && ln -sf st-srs/${_ST_OBJ} st
    )
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "Build state-threads failed, ret=$ret"; exit $ret; fi
# Always update the links.
(cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf st && ln -sf st-srs/${_ST_OBJ} st)
(cd ${SRS_OBJS} && rm -rf st && ln -sf ${SRS_PLATFORM}/st-srs/${_ST_OBJ} st)
if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "Build state-threads static lib failed."; exit -1; fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
function write_nginx_html5()
{
    cat<<END > ${html_file}
<video width="100%" autoplay controls autobuffer type="application/vnd.apple.mpegurl"
    src="${hls_stream}">
</video>
END
}
# create the nginx dir, for http-server if not build nginx
mkdir -p ${SRS_OBJS}/nginx

# the demo dir.
# create forward dir
mkdir -p ${SRS_OBJS}/nginx/html/live &&
html_file=${SRS_OBJS}/nginx/html/live/livestream.html && hls_stream=livestream.m3u8 && write_nginx_html5

# copy players to nginx html dir.
rm -rf ${SRS_OBJS}/nginx/html/players &&
ln -sf `pwd`/research/players ${SRS_OBJS}/nginx/html/players

# for favicon.ico
rm -rf ${SRS_OBJS}/nginx/html/favicon.ico &&
ln -sf `pwd`/research/api-server/static-dir/favicon.ico ${SRS_OBJS}/nginx/html/favicon.ico

# For srs-console.
rm -rf ${SRS_OBJS}/nginx/html/console &&
ln -sf `pwd`/research/console ${SRS_OBJS}/nginx/html/console

# For SRS signaling.
rm -rf ${SRS_OBJS}/nginx/html/demos &&
ln -sf `pwd`/3rdparty/signaling/www/demos ${SRS_OBJS}/nginx/html/demos

# For home page index.html
rm -rf ${SRS_OBJS}/nginx/html/index.html &&
ln -sf `pwd`/research/api-server/static-dir/index.html ${SRS_OBJS}/nginx/html/index.html

# nginx.html to detect whether nginx is alive
echo "Nginx is ok." > ${SRS_OBJS}/nginx/html/nginx.html

#####################################################################################
# Generate default self-sign certificate for HTTPS server, test only.
#####################################################################################
if [[ ! -f conf/server.key || ! -f conf/server.crt ]]; then
    openssl genrsa -out conf/server.key 2048
    openssl req -new -x509 -key conf/server.key -out conf/server.crt -days 3650 -subj "/C=CN/ST=Beijing/L=Beijing/O=Me/OU=Me/CN=ossrs.net"
    echo "Generate test-only self-sign certificate files"
fi

#####################################################################################
# cherrypy for http hooks callback, CherryPy-3.2.4
#####################################################################################
if [[ $SRS_CHERRYPY == YES ]]; then
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
        OPENSSL_CONFIG="./Configure linux-generic32"
        if [[ $SRS_CROSS_BUILD_ARCH == "arm" ]]; then OPENSSL_CONFIG="./Configure linux-armv4"; fi
        if [[ $SRS_CROSS_BUILD_ARCH == "aarch64" ]]; then OPENSSL_CONFIG="./Configure linux-aarch64"; fi
    elif [[ ! -f ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib/libssl.a ]]; then
        # Try to use exists libraries.
        if [[ -f /usr/local/ssl/lib/libssl.a && $SRS_SSL_LOCAL == NO ]]; then
            (mkdir -p  ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib &&
                ln -sf /usr/local/ssl/lib/libssl.a && ln -sf /usr/local/ssl/lib/libcrypto.a &&
                mkdir -p /usr/local/ssl/lib/pkgconfig && ln -sf /usr/local/ssl/lib/pkgconfig)
            (mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include && cd ${SRS_OBJS}/${SRS_PLATFORM}/openssl/include &&
                ln -sf /usr/local/ssl/include/openssl)
        fi
        # Warning if not use the system ssl.
        if [[ -f /usr/local/ssl/lib/libssl.a && $SRS_SSL_LOCAL == YES ]]; then
            echo "Warning: Local openssl is on, ignore system openssl"
        fi
    fi
    # For RTC, we should use ASM to improve performance, not a little improving.
    if [[ $SRS_RTC == NO || $SRS_NASM == NO ]]; then
        OPENSSL_OPTIONS="$OPENSSL_OPTIONS -no-asm"
        echo "Warning: NASM is off, performance is hurt"
    fi
    # Mac OS X can have issues (its often a neglected platform).
    # @see https://wiki.openssl.org/index.php/Compilation_and_Installation
    if [[ $SRS_OSX == YES ]]; then
        export KERNEL_BITS=64;
    fi
    # Use 1.0 if required.
    if [[ $SRS_SSL_1_0 == YES ]]; then
        OPENSSL_AR="$SRS_TOOL_AR -r" # For openssl 1.0, MUST specifies the args for ar or build faild.
        OPENSSL_CANDIDATE="openssl-OpenSSL_1_0_2u" && OPENSSL_UNZIP="tar xf ../../3rdparty/$OPENSSL_CANDIDATE.tar.gz"
    else
        OPENSSL_AR="$SRS_TOOL_AR"
        OPENSSL_CANDIDATE="openssl-1.1-fit" && OPENSSL_UNZIP="cp -R ../../3rdparty/$OPENSSL_CANDIDATE ."
    fi
    #
    # https://wiki.openssl.org/index.php/Compilation_and_Installation#Configure_Options
    # Already defined: -no-shared -no-threads -no-asm
    # Should enable:  -no-dtls -no-dtls1 -no-ssl3
    # Might able to disable: -no-ssl2 -no-comp -no-idea -no-hw -no-engine -no-dso -no-err -no-nextprotoneg -no-psk -no-srp -no-ec2m -no-weak-ssl-ciphers
    # Note that we do not disable more features, because no file could be removed.
    #OPENSSL_OPTIONS="$OPENSSL_OPTIONS -no-ssl2 -no-comp -no-idea -no-hw -no-engine -no-dso -no-err -no-nextprotoneg -no-psk -no-srp -no-ec2m -no-weak-ssl-ciphers"
    #
    # cross build not specified, if exists flag, need to rebuild for no-arm platform.
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/$OPENSSL_CANDIDATE/_release/lib/libssl.a ]]; then
        echo "The $OPENSSL_CANDIDATE is ok.";
    else
        echo "Building $OPENSSL_CANDIDATE.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            ${OPENSSL_UNZIP} && cd $OPENSSL_CANDIDATE && ${OPENSSL_CONFIG} --prefix=`pwd`/_release $OPENSSL_OPTIONS &&
            make CC=${SRS_TOOL_CC} AR="${OPENSSL_AR}" LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB} ${SRS_JOBS} && make install_sw &&
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
    (cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf openssl && ln -sf $OPENSSL_CANDIDATE/_release openssl)
    (cd ${SRS_OBJS} && rm -rf openssl && ln -sf ${SRS_PLATFORM}/${OPENSSL_LIB} openssl)
    if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "Build $OPENSSL_CANDIDATE failed."; exit -1; fi
fi

#####################################################################################
# srtp
#####################################################################################
SRTP_OPTIONS=""
# If use ASM for SRTP, we enable openssl(with ASM).
if [[ $SRS_SRTP_ASM == YES ]]; then
    SRTP_OPTIONS="--enable-openssl"
    SRTP_CONFIGURE="env PKG_CONFIG_PATH=$(cd ${SRS_OBJS}/${SRS_PLATFORM} && pwd)/openssl/lib/pkgconfig ./configure"
else
    SRTP_CONFIGURE="./configure"
fi
if [[ $SRS_CROSS_BUILD == YES ]]; then
    SRTP_OPTIONS="$SRTP_OPTIONS --host=$SRS_CROSS_BUILD_HOST"
fi
# Patched ST from https://github.com/ossrs/state-threads/tree/srs
if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/_release/lib/libsrtp2.a ]]; then
    echo "The libsrtp-2-fit is ok.";
else
    echo "Building libsrtp-2-fit.";
    (
        rm -rf ${SRS_OBJS}/srtp2 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
        rm -rf libsrtp-2-fit && cp -R ../../3rdparty/libsrtp-2-fit . && cd libsrtp-2-fit &&
        patch -p0 crypto/math/datatypes.c ../../../3rdparty/patches/srtp/gcc10-01.patch &&
        $SRTP_CONFIGURE ${SRTP_OPTIONS} --prefix=`pwd`/_release &&
        make ${SRS_JOBS} && make install &&
        cd .. && rm -rf srtp2 && ln -sf libsrtp-2-fit/_release srtp2
    )
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "Build libsrtp-2-fit failed, ret=$ret"; exit $ret; fi
# Always update the links.
(cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf srtp2 && ln -sf libsrtp-2-fit/_release srtp2)
(cd ${SRS_OBJS} && rm -rf srtp2 && ln -sf ${SRS_PLATFORM}/libsrtp-2-fit/_release srtp2)
if [ ! -f ${SRS_OBJS}/srtp2/lib/libsrtp2.a ]; then echo "Build libsrtp-2-fit static lib failed."; exit -1; fi

#####################################################################################
# libopus, for WebRTC to transcode AAC with Opus.
#####################################################################################
# For cross build, we use opus of FFmpeg, so we don't build the libopus.
if [[ $SRS_RTC == YES && $SRS_CROSS_BUILD == NO ]]; then
    # Only build static libraries if no shared FFmpeg.
    if [[ $SRS_SHARED_FFMPEG == NO ]]; then
        OPUS_OPTIONS="--disable-shared --disable-doc"
    fi
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1/_release/lib/libopus.a ]]; then
        echo "The opus-1.3.1 is ok.";
    else
        echo "Building opus-1.3.1.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            tar xf ../../3rdparty/opus-1.3.1.tar.gz && cd opus-1.3.1 &&
            ./configure --prefix=`pwd`/_release --enable-static $OPUS_OPTIONS &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -rf opus && ln -sf opus-1.3.1/_release opus
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build opus-1.3.1 failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf opus && ln -sf opus-1.3.1/_release opus)
    (cd ${SRS_OBJS} && rm -rf opus && ln -sf ${SRS_PLATFORM}/opus-1.3.1/_release opus)
    if [ ! -f ${SRS_OBJS}/opus/lib/libopus.a ]; then echo "Build opus-1.3.1 failed."; exit -1; fi
fi

#####################################################################################
# ffmpeg-fit, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_FFMPEG_FIT == YES ]]; then
    FFMPEG_OPTIONS=""
    if [[ $SRS_CROSS_BUILD == YES ]]; then
      FFMPEG_CONFIGURE=./configure
    else
      FFMPEG_CONFIGURE="env PKG_CONFIG_PATH=$(cd ${SRS_OBJS}/${SRS_PLATFORM} && pwd)/opus/lib/pkgconfig ./configure"
    fi

    # If disable nasm, disable all ASMs.
    nasm -v >/dev/null 2>&1 && NASM_BIN_OK=YES
    if [[ $NASM_BIN_OK != YES || $SRS_NASM == NO || $SRS_CROSS_BUILD == YES ]]; then
        FFMPEG_OPTIONS="--disable-asm --disable-x86asm --disable-inline-asm"
    fi
    # Only build static libraries if no shared FFmpeg.
    if [[ $SRS_SHARED_FFMPEG == YES ]]; then
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-shared"
    fi
    # For cross-build.
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-cross-compile --target-os=linux"
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --arch=$SRS_CROSS_BUILD_ARCH";
        if [[ $SRS_CROSS_BUILD_CPU != "" ]]; then FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cpu=$SRS_CROSS_BUILD_CPU"; fi
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cross-prefix=$SRS_CROSS_BUILD_PREFIX"
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cc=${SRS_TOOL_CC} --cxx=${SRS_TOOL_CXX} --ar=${SRS_TOOL_AR} --ld=${_ST_LD}"
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=opus --enable-encoder=opus"
    else
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=libopus --enable-encoder=libopus --enable-libopus"
    fi

    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/_release/lib/libavcodec.a ]]; then
        echo "The ffmpeg-4-fit is ok.";
    else
        echo "Building ffmpeg-4-fit.";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit && mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit &&
            # Create a hidden directory .src
            cd ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit && cp -R ../../../3rdparty/ffmpeg-4-fit/* . &&
            # Build source code.
            $FFMPEG_CONFIGURE \
              --prefix=`pwd`/_release --pkg-config=pkg-config \
              --pkg-config-flags="--static" --extra-libs="-lpthread" --extra-libs="-lm" \
              --disable-everything ${FFMPEG_OPTIONS} \
              --disable-programs --disable-doc --disable-htmlpages --disable-manpages --disable-podpages --disable-txtpages \
              --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network \
              --disable-dct --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils \
              --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox --disable-cuvid \
              --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-v4l2-m2m --disable-vaapi \
              --disable-vdpau --disable-appkit --disable-coreimage --disable-avfoundation --disable-securetransport --disable-iconv \
              --disable-lzma --disable-sdl2 --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm \
              --enable-encoder=aac &&
            # See https://www.laoyuyu.me/2019/05/23/android/clang_compile_ffmpeg/
            if [[ $SRS_CROSS_BUILD == YES ]]; then
              sed -i -e 's/#define getenv(x) NULL/\/\*#define getenv(x) NULL\*\//g' config.h &&
              sed -i -e 's/#define HAVE_GMTIME_R 0/#define HAVE_GMTIME_R 1/g' config.h &&
              sed -i -e 's/#define HAVE_LOCALTIME_R 0/#define HAVE_LOCALTIME_R 1/g' config.h
            fi &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -rf ffmpeg && ln -sf ffmpeg-4-fit/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-4-fit failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf ffmpeg && ln -sf ffmpeg-4-fit/_release ffmpeg)
    (cd ${SRS_OBJS} && rm -rf ffmpeg && ln -sf ${SRS_PLATFORM}/ffmpeg-4-fit/_release ffmpeg)
    if [ ! -f ${SRS_OBJS}/ffmpeg/lib/libavcodec.a ]; then echo "Build ffmpeg-4-fit failed."; exit -1; fi
fi

#####################################################################################
# live transcoding, ffmpeg-4.1, x264-core157, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
# Guess whether the ffmpeg is.
SYSTEMP_FFMPEG_BIN=/usr/local/bin/ffmpeg
if [[ ! -f $SYSTEMP_FFMPEG_BIN ]]; then SYSTEMP_FFMPEG_BIN=/usr/local/ffmpeg/bin/ffmpeg; fi
# Always link the ffmpeg tools if exists.
if [[ -f $SYSTEMP_FFMPEG_BIN && ! -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg ]]; then
    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin &&
    ln -sf $SYSTEMP_FFMPEG_BIN ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin/ffmpeg &&
    (cd ${SRS_OBJS} && rm -rf ffmpeg && ln -sf ${SRS_PLATFORM}/ffmpeg)
fi
if [ $SRS_FFMPEG_TOOL = YES ]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg/bin/ffmpeg ]]; then
        echo "ffmpeg-4.1 is ok.";
    else
        echo -e "${RED}Error: No FFmpeg found at /usr/local/bin/ffmpeg${BLACK}"
        echo -e "${RED}    Please copy it from srs-docker${BLACK}"
        echo -e "${RED}    or download from http://ffmpeg.org/download.html${BLACK}"
        echo -e "${RED}    or disable it by --without-ffmpeg${BLACK}"
        exit -1;
    fi
    # Always update the links.
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg ]]; then
        (cd ${SRS_OBJS} && rm -rf ffmpeg && ln -sf ${SRS_PLATFORM}/ffmpeg)
    fi
fi

#####################################################################################
# SRT module, https://github.com/ossrs/srs/issues/1147#issuecomment-577469119
#####################################################################################
if [[ $SRS_SRT == YES ]]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/srt/lib/libsrt.a ]]; then
        echo "libsrt-1-fit is ok.";
    else
        echo "Build srt-1-fit"
        (
            if [[ ! -d ${SRS_OBJS}/${SRS_PLATFORM}/openssl/lib/pkgconfig ]]; then
                echo "OpenSSL pkgconfig no found, build srt-1-fit failed.";
                exit -1;
            fi
            # Always disable c++11 for libsrt, because only the srt-app requres it.
            LIBSRT_OPTIONS="--disable-app  --enable-static --enable-c++11=0"
            if [[ $SRS_SHARED_SRT == YES ]]; then
                LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=1"
            else
                LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=0"
            fi
            # Start build libsrt.
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            cp -R ../../3rdparty/srt-1-fit srt-1-fit && cd srt-1-fit &&
            PKG_CONFIG_PATH=../openssl/lib/pkgconfig ./configure --prefix=`pwd`/_release $LIBSRT_OPTIONS &&
            make ${SRS_JOBS} && make install &&
            cd .. && rm -rf srt && ln -sf srt-1-fit/_release srt &&
            # If exists lib64 of libsrt, link it to lib
            if [[ -d srt/lib64 ]]; then
                cd srt && ln -sf lib64 lib
            fi
        )
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build srt-1-fit failed, ret=$ret"; exit $ret; fi
    fi
    # Always update the links.
    (cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf srt && ln -sf srt-1-fit/_release srt)
    (cd ${SRS_OBJS} && rm -rf srt && ln -sf ${SRS_PLATFORM}/srt-1-fit/_release srt)
    if [ ! -f ${SRS_OBJS}/srt/lib/libsrt.a ]; then echo "Build srt-1-fit failed."; exit -1; fi
fi

#####################################################################################
# build utest code
#####################################################################################
if [ $SRS_UTEST = YES ]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/gtest-1.6.0/include/gtest/gtest.h ]]; then
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
    (cd ${SRS_OBJS}/${SRS_PLATFORM} && rm -rf gtest && ln -sf gtest-1.6.0 gtest)
    (cd ${SRS_OBJS} && rm -rf gtest && ln -sf ${SRS_PLATFORM}/gtest-1.6.0 gtest)
    if [ ! -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]; then echo "Build gtest-1.6.0 failed."; exit -1; fi
fi

#####################################################################################
# build gperf code
#####################################################################################
if [ $SRS_GPERF = YES ]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/gperf/bin/pprof ]]; then
        echo "The gperftools-2-fit is ok.";
    else
        echo "Build gperftools-2-fit";
        (
            rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2-fit && cd ${SRS_OBJS}/${SRS_PLATFORM} &&
            cp -R ../../3rdparty/gperftools-2-fit . && cd gperftools-2-fit &&
            ./configure --prefix=`pwd`/_release --enable-frame-pointers && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf gperf && ln -sf gperftools-2-fit/_release gperf &&
            rm -rf pprof && ln -sf gperf/bin/pprof pprof
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gperftools-2-fit failed, ret=$ret"; exit $ret; fi
    # Always update the links.
    (cd ${SRS_OBJS} && rm -rf pprof && ln -sf ${SRS_PLATFORM}/gperf/bin/pprof pprof)
    (cd ${SRS_OBJS} && rm -rf gperf && ln -sf ${SRS_PLATFORM}/gperftools-2-fit/_release gperf)
    if [ ! -f ${SRS_OBJS}/pprof ]; then echo "Build gperftools-2-fit failed."; exit -1; fi
fi
