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
# Check OS and CPU architectures.
#####################################################################################
if [[ $OS_IS_UBUNTU != YES && $OS_IS_CENTOS != YES && $OS_IS_OSX != YES && $SRS_CYGWIN64 != YES ]]; then
    if [[ $SRS_CROSS_BUILD != YES && $SRS_GENERIC_LINUX != YES ]]; then
        echo "Your OS `uname -s` is not supported."
        if [[ $(uname -s) == "Linux" ]]; then
            echo "Please try --generic-linux=on for other Linux systems."
        fi
        exit 1
    fi
fi

# The absolute path of SRS_OBJS, for prefix and PKG_CONFIG_PATH
SRS_DEPENDS_LIBS=$(mkdir -p $SRS_OBJS && cd $SRS_OBJS && pwd)
echo -n "SRS_JOBS: $SRS_JOBS, SRS_DEPENDS_LIBS: ${SRS_DEPENDS_LIBS}"
if [[ ! -z $OS_IS_LINUX ]]; then echo -n ", OS_IS_LINUX: $OS_IS_LINUX"; fi
if [[ ! -z $OS_IS_OSX ]]; then echo -n ", OS_IS_OSX: $OS_IS_OSX"; fi
if [[ ! -z $OS_IS_CYGWIN ]]; then echo -n ", OS_IS_CYGWIN: $OS_IS_CYGWIN"; fi
if [[ ! -z $OS_IS_UBUNTU ]]; then echo -n ", OS_IS_UBUNTU: $OS_IS_UBUNTU"; fi
if [[ ! -z $OS_IS_CENTOS ]]; then echo -n ", OS_IS_CENTOS: $OS_IS_CENTOS"; fi
if [[ ! -z $SRS_CROSS_BUILD ]]; then echo -n ", SRS_CROSS_BUILD: $SRS_CROSS_BUILD"; fi
if [[ ! -z $OS_IS_LOONGARCH64 ]]; then echo -n ", OS_IS_LOONGARCH64: $OS_IS_LOONGARCH64"; fi
if [[ ! -z $OS_IS_MIPS64 ]]; then echo -n ", OS_IS_MIPS64: $OS_IS_MIPS64"; fi
if [[ ! -z $OS_IS_LOONGSON ]]; then echo -n ", OS_IS_LOONGSON: $OS_IS_LOONGSON"; fi
if [[ ! -z $OS_IS_X86_64 ]]; then echo -n ", OS_IS_X86_64: $OS_IS_X86_64"; fi
if [[ ! -z $OS_IS_RISCV ]]; then echo -n ", OS_IS_RISCV: $OS_IS_RISCV"; fi
echo ""

#####################################################################################
# Check dependency tools.
#####################################################################################
if [[ $SRS_OSX == YES ]]; then
    brew --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Please install brew at https://brew.sh/"; exit $ret;
    fi
fi
# Check perl, which is depended by automake for building libopus etc.
perl --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install perl by:"
        echo "  yum install -y perl"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install perl by:"
        echo "  apt install -y perl"
    else
        echo "Please install perl"
    fi
    exit $ret;
fi
gcc --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install gcc by:"
        echo "  yum install -y gcc"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install gcc by:"
        echo "  apt install -y gcc"
    else
        echo "Please install gcc"
    fi
    exit $ret;
fi
g++ --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install g++ by:"
        echo "  yum install -y gcc-c++"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install g++ by:"
        echo "  apt install -y g++"
    else
        echo "Please install gcc-c++"
    fi
    exit $ret;
fi
make --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install make by:"
        echo "  yum install -y make"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install make by:"
        echo "  apt install -y make"
    else
        echo "Please install make"
    fi
    exit $ret;
fi
patch --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install patch by:"
        echo "  yum install -y patch"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install patch by:"
        echo "  apt install -y patch"
    else
        echo "Please install patch"
    fi
    exit $ret;
fi
unzip -v >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install unzip by:"
        echo "  yum install -y unzip"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install unzip by:"
        echo "  apt install -y unzip"
    else
        echo "Please install unzip"
    fi
    exit $ret;
fi
automake --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install automake by:"
        echo "  yum install -y automake"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install automake by:"
        echo "  apt install -y automake"
    else
        echo "Please install automake"
    fi
    exit $ret;
fi
if [[ $SRS_VALGRIND == YES ]]; then
    valgrind --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Please install valgrind"; exit $ret;
    fi
    if [[ ! -f /usr/include/valgrind/valgrind.h ]]; then
        echo "Please install valgrind-dev"; exit $ret;
    fi
fi
# Check tclsh, which is depended by SRT.
if [[ $SRS_SRT == YES ]]; then
    tclsh <<< "exit" >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        if [[ $OS_IS_CENTOS == YES ]]; then
            echo "Please install tclsh by:"
            echo "  yum install -y tcl"
        elif [[ $OS_IS_UBUNTU == YES ]]; then
            echo "Please install tclsh by:"
            echo "  apt install -y tclsh"
        else
            echo "Please install tclsh"
        fi
        exit $ret;
    fi
    cmake --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
        if [[ $OS_IS_CENTOS == YES ]]; then
            echo "Please install cmake by:"
            echo "  yum install -y cmake"
        elif [[ $OS_IS_UBUNTU == YES ]]; then
            echo "Please install cmake by:"
            echo "  apt install -y cmake"
        else
            echo "Please install cmake"
        fi
        exit $ret;
    fi
fi
pkg-config --version >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    echo "Please install pkg-config"; exit $ret;
fi
which ls >/dev/null 2>/dev/null; ret=$?; if [[ 0 -ne $ret ]]; then
    if [[ $OS_IS_CENTOS == YES ]]; then
        echo "Please install which by:"
        echo "  yum install -y which"
    elif [[ $OS_IS_UBUNTU == YES ]]; then
        echo "Please install which by:"
        echo "  apt install -y which"
    else
        echo "Please install which"
    fi
    exit $ret;
fi

#####################################################################################
# Try to load cache if exists /usr/local/srs-cache
#####################################################################################
# Use srs-cache from base image. See https://github.com/ossrs/dev-docker/blob/ubuntu20-cache/Dockerfile
# Note that the cache for cygwin is not under /usr/local, but copy to objs instead.
if [[ -d /usr/local/srs-cache/srs/trunk/objs && $(pwd) != "/usr/local/srs-cache/srs/trunk" && $SRS_BUILD_CACHE == YES ]]; then
    SOURCE_DIR=$(ls -d /usr/local/srs-cache/srs/trunk/objs/Platform-SRS${SRS_MAJOR}-* 2>/dev/null|head -n 1)
    if [[ -d $SOURCE_DIR ]]; then
        TARGET_DIR=${SRS_OBJS}/${SRS_PLATFORM} &&
        echo "Build from cache, source=$SOURCE_DIR, target=$TARGET_DIR" &&
        rm -rf $TARGET_DIR && mkdir -p ${SRS_OBJS} && cp -R $SOURCE_DIR $TARGET_DIR &&
        du -sh /usr/local/srs-cache/srs/trunk/objs/Platform-* &&
        du -sh /usr/local/srs-cache/srs/trunk/objs/Platform-*/* &&
        du -sh objs/Platform-* &&
        ls -lh objs
    fi
fi

#####################################################################################
# Check for address sanitizer, see https://github.com/google/sanitizers
#####################################################################################
if [[ $SRS_SANITIZER == YES ]]; then
    echo 'int main() { return 0; }' > ${SRS_OBJS}/test_sanitizer.c &&
    gcc -fsanitize=address -fno-omit-frame-pointer -g -O0 ${SRS_OBJS}/test_sanitizer.c \
        -o ${SRS_OBJS}/test_sanitizer 1>/dev/null 2>&1;
    ret=$?; rm -rf ${SRS_OBJS}/test_sanitizer*
    if [[ $ret -ne 0 ]]; then
        echo "Please install libasan, see https://github.com/google/sanitizers";
        if [[ $OS_IS_CENTOS == YES ]]; then echo "    sudo yum install -y libasan"; fi
        exit $ret;
    fi
fi

if [[ $SRS_SANITIZER == YES && $SRS_SANITIZER_STATIC == NO ]]; then
    echo 'int main() { return 0; }' > ${SRS_OBJS}/test_sanitizer.c &&
    gcc -fsanitize=address -fno-omit-frame-pointer -static-libasan -g -O0 ${SRS_OBJS}/test_sanitizer.c \
        -o ${SRS_OBJS}/test_sanitizer 1>/dev/null 2>&1;
    ret=$?; rm -rf ${SRS_OBJS}/test_sanitizer*
    if [[ $ret -eq 0 ]]; then
        echo "link static-libasan"
        SRS_SANITIZER_STATIC=YES
    fi
fi

if [[ $SRS_SANITIZER == YES && $SRS_SANITIZER_LOG == NO ]]; then
    echo "#include <sanitizer/asan_interface.h>" > ${SRS_OBJS}/test_sanitizer.c &&
    echo "int main() { return 0; }" >> ${SRS_OBJS}/test_sanitizer.c &&
    gcc -fsanitize=address -fno-omit-frame-pointer -g -O0 ${SRS_OBJS}/test_sanitizer.c \
        -o ${SRS_OBJS}/test_sanitizer 1>/dev/null 2>&1;
    ret=$?; rm -rf ${SRS_OBJS}/test_sanitizer*
    if [[ $ret -eq 0 ]]; then
        echo "libasan api found ok!";
        SRS_SANITIZER_LOG=YES
    fi
fi

#####################################################################################
# state-threads
#####################################################################################
# check the cross build flag file, if flag changed, need to rebuild the st.
_ST_MAKE=linux-debug && _ST_OBJ="LINUX_`uname -r`_DBG"
# Always alloc on heap, @see https://github.com/ossrs/srs/issues/509#issuecomment-719931676
_ST_EXTRA_CFLAGS="-DMALLOC_STACK"
# For valgrind to detect memory issues.
if [[ $SRS_VALGRIND == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_VALGRIND"
fi
# for osx, use darwin for st, donot use epoll.
if [[ $SRS_OSX == YES ]]; then
    _ST_MAKE=darwin-debug && _ST_OBJ="DARWIN_`uname -r`_DBG"
fi
# for windows/cygwin
if [[ $SRS_CYGWIN64 = YES ]]; then
    _ST_MAKE=cygwin64-debug && _ST_OBJ="CYGWIN64_`uname -s`_DBG"
fi
# For Ubuntu, the epoll detection might be fail.
if [[ $OS_IS_UBUNTU == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_HAVE_EPOLL"
fi
# Whether enable debug stats.
if [[ $SRS_DEBUG_STATS == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DDEBUG_STATS"
fi
# Whether to enable asan.
if [[ $SRS_SANITIZER == YES ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS -DMD_ASAN -fsanitize=address -fno-omit-frame-pointer"
fi
# Pass the global extra flags.
if [[ $SRS_EXTRA_FLAGS != '' ]]; then
    _ST_EXTRA_CFLAGS="$_ST_EXTRA_CFLAGS $SRS_EXTRA_FLAGS"
fi
# Whether link as .so
if [[ $SRS_SHARED_ST == YES ]]; then
  _ST_STATIC_ONLY=no;
else
  _ST_STATIC_ONLY=yes;
fi
# The final args to make st.
_ST_MAKE_ARGS="${_ST_MAKE} STATIC_ONLY=${_ST_STATIC_ONLY}"
_ST_MAKE_ARGS="${_ST_MAKE_ARGS} CC=${SRS_TOOL_CC} AR=${SRS_TOOL_AR} LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB}"
# Patched ST from https://github.com/ossrs/state-threads/tree/srs
if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st/libst.a ]]; then
    rm -rf ${SRS_OBJS}/st && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st ${SRS_OBJS}/ &&
    echo "The state-threads is ok."
else
    echo "Building state-threads." &&
    rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/st-srs ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st ${SRS_OBJS}/st &&
    cp -rf ${SRS_WORKDIR}/3rdparty/st-srs ${SRS_OBJS}/${SRS_PLATFORM}/ &&
    env EXTRA_CFLAGS="${_ST_EXTRA_CFLAGS}" make -C ${SRS_OBJS}/${SRS_PLATFORM}/st-srs ${_ST_MAKE_ARGS} &&
    mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st &&
    cp -f ${SRS_OBJS}/${SRS_PLATFORM}/st-srs/${_ST_OBJ}/st.h ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st/ &&
    cp -f ${SRS_OBJS}/${SRS_PLATFORM}/st-srs/${_ST_OBJ}/libst.a ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st/ &&
    cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/st ${SRS_OBJS}/ &&
    echo "The state-threads is ok."
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "Build state-threads failed, ret=$ret"; exit $ret; fi

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
html_file=${SRS_OBJS}/nginx/html/live/livestream.html && hls_stream=livestream.m3u8 && write_nginx_html5 &&

# copy players to nginx html dir.
rm -rf ${SRS_OBJS}/nginx/html/players &&
cp -rf $SRS_WORKDIR/research/players ${SRS_OBJS}/nginx/html/ &&

# for favicon.ico
rm -rf ${SRS_OBJS}/nginx/html/favicon.ico &&
cp -f $SRS_WORKDIR/research/api-server/static-dir/favicon.ico ${SRS_OBJS}/nginx/html/favicon.ico &&

# For srs-console.
rm -rf ${SRS_OBJS}/nginx/html/console &&
cp -rf $SRS_WORKDIR/research/console ${SRS_OBJS}/nginx/html/ &&

# For SRS signaling.
rm -rf ${SRS_OBJS}/nginx/html/demos &&
cp -rf $SRS_WORKDIR/3rdparty/signaling/www/demos ${SRS_OBJS}/nginx/html/ &&

# For home page index.html
rm -rf ${SRS_OBJS}/nginx/html/index.html &&
cp -f $SRS_WORKDIR/research/api-server/static-dir/index.html ${SRS_OBJS}/nginx/html/index.html &&

# nginx.html to detect whether nginx is alive
echo "Nginx is ok." > ${SRS_OBJS}/nginx/html/nginx.html

# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "Build web pages failed, ret=$ret"; exit $ret; fi

#####################################################################################
# Generate default self-sign certificate for HTTPS server, test only.
#####################################################################################
if [[ ! -f $SRS_WORKDIR/conf/server.key || ! -f $SRS_WORKDIR/conf/server.crt ]]; then
    openssl genrsa -out $SRS_WORKDIR/conf/server.key 2048 &&
    openssl req -new -x509 -key $SRS_WORKDIR/conf/server.key -out $SRS_WORKDIR/conf/server.crt -days 3650 \
        -subj "/C=CN/ST=Beijing/L=Beijing/O=Me/OU=Me/CN=ossrs.net" &&
    echo "Generate test-only self-sign certificate files"
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
        if [[ $SRS_CROSS_BUILD_ARCH == "mipsel" ]]; then OPENSSL_CONFIG="./Configure linux-mips32"; fi
    elif [[ ! -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/lib/libssl.a ]]; then
        # Try to use exists libraries.
        if [[ -f /usr/local/ssl/lib/libssl.a && $SRS_SSL_LOCAL == NO ]]; then
            (mkdir -p  ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/lib && cd ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/lib &&
                cp /usr/local/ssl/lib/libssl.a . && cp /usr/local/ssl/lib/libcrypto.a . &&
                mkdir -p /usr/local/ssl/lib/pkgconfig && cp -rf /usr/local/ssl/lib/pkgconfig .)
            (mkdir -p ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/include && cd ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/include &&
                cp -rf /usr/local/ssl/include/openssl .)
        fi
        # Warning if not use the system ssl.
        if [[ -f /usr/local/ssl/lib/libssl.a && $SRS_SSL_LOCAL == YES ]]; then
            echo "Warning: Local openssl is on, ignore system openssl"
        fi
    fi
    # Patch for loongarch mips64, disable ASM for build failed message as bellow:
    #       Error: opcode not supported on this processor: mips3 (mips3)
    if [[ $OS_IS_MIPS64 == YES ]]; then OPENSSL_CONFIG="./Configure linux64-mips64"; fi
    if [[ $OS_IS_LOONGSON == YES ]]; then OPENSSL_OPTIONS="$OPENSSL_OPTIONS -no-asm"; fi
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
        OPENSSL_CANDIDATE="openssl-OpenSSL_1_0_2u" &&
        OPENSSL_UNZIP="tar xf ${SRS_WORKDIR}/3rdparty/$OPENSSL_CANDIDATE.tar.gz -C ${SRS_OBJS}/${SRS_PLATFORM}"
    else
        OPENSSL_AR="$SRS_TOOL_AR"
        OPENSSL_CANDIDATE="openssl-1.1-fit" &&
        OPENSSL_UNZIP="cp -R ${SRS_WORKDIR}/3rdparty/$OPENSSL_CANDIDATE ${SRS_OBJS}/${SRS_PLATFORM}/"
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
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl/lib/libssl.a ]]; then
        rm -rf ${SRS_OBJS}/openssl && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl ${SRS_OBJS}/ &&
        echo "The $OPENSSL_CANDIDATE is ok."
    else
        echo "Building $OPENSSL_CANDIDATE." &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl \
            ${SRS_OBJS}/openssl &&
        ${OPENSSL_UNZIP} &&
        (
            cd ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} &&
            chmod +x ./config ./Configure &&
            ${OPENSSL_CONFIG} --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/openssl $OPENSSL_OPTIONS
        ) &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} CC=${SRS_TOOL_CC} AR="${OPENSSL_AR}" \
            LD=${SRS_TOOL_LD} RANDLIB=${SRS_TOOL_RANDLIB} ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/${OPENSSL_CANDIDATE} install_sw &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/openssl ${SRS_OBJS}/ &&
        echo "The $OPENSSL_CANDIDATE is ok."
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build $OPENSSL_CANDIDATE failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# srtp
#####################################################################################
if [[ $SRS_RTC == YES && $SRS_USE_SYS_SRTP == YES ]]; then
    echo "Warning: Use system libsrtp, without compiling srtp."
fi
if [[ $SRS_RTC == YES && $SRS_USE_SYS_SRTP == NO ]]; then
    SRTP_OPTIONS=""
    # To eliminate warnings, see https://stackoverflow.com/a/34208904/17679565
    #       was built for newer macOS version (11.6) than being linked (11.0)
    if [[ $SRS_OSX == YES ]]; then
        export MACOSX_DEPLOYMENT_TARGET=11.0
        echo "Set MACOSX_DEPLOYMENT_TARGET to avoid warnings"
    fi
    # If use ASM for SRTP, we enable openssl(with ASM).
    if [[ $SRS_SRTP_ASM == YES ]]; then
        SRTP_OPTIONS="--enable-openssl"
        SRTP_CONFIGURE="env PKG_CONFIG_PATH=${SRS_DEPENDS_LIBS}/openssl/lib/pkgconfig ./configure"
    else
        SRTP_OPTIONS="--disable-openssl"
        SRTP_CONFIGURE="./configure"
    fi
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        SRTP_OPTIONS="$SRTP_OPTIONS --host=$SRS_CROSS_BUILD_HOST"
    fi
    if [[ $OS_IS_LOONGARCH64 == YES ]]; then
        SRTP_OPTIONS="$SRTP_OPTIONS --build=loongarch64-unknown-linux-gnu"
    fi
    # Copy and patch source files, then build and install libsrtp.
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srtp2/lib/libsrtp2.a ]]; then
        rm -rf ${SRS_OBJS}/srtp2 &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srtp2 ${SRS_OBJS} &&
        echo "The libsrtp-2-fit is ok."
    else
        echo "Building libsrtp-2-fit."
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srtp2 \
            ${SRS_OBJS}/srtp2 &&
        cp -rf ${SRS_WORKDIR}/3rdparty/libsrtp-2-fit ${SRS_OBJS}/${SRS_PLATFORM}/ &&
        # For cygwin64, the patch is not available, so use sed instead.
        if [[ $SRS_CYGWIN64 == YES ]]; then
            sed -i 's/char bit_string/static char bit_string/g' ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/crypto/math/datatypes.c
        else
            patch -p0 ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/crypto/math/datatypes.c ${SRS_WORKDIR}/3rdparty/patches/srtp/gcc10-01.patch
        fi &&
        # Patch the cpu arch guessing for RISCV.
        if [[ $OS_IS_RISCV == YES ]]; then
            patch -p0 ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/config.guess ${SRS_WORKDIR}/3rdparty/patches/srtp/config.guess-02.patch
        fi &&
        (
            cd ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit &&
            $SRTP_CONFIGURE ${SRTP_OPTIONS} --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/srtp2
        ) &&
        # Sometimes it might fail because autoconf failed to generate crypto/include.config.h
        if [[ $SRS_CYGWIN64 == YES ]]; then
            SRS_PATCH_SOURCE=${SRS_WORKDIR}/3rdparty/patches/srtp/cygwin-crypto-include-config.h
            if [[ $SRS_SRTP_ASM == YES ]]; then
                SRS_PATCH_SOURCE=${SRS_WORKDIR}/3rdparty/patches/srtp/cygwin-gcm-crypto-include-config.h
            fi
            grep -q 'HAVE_UINT64_T 1' ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/crypto/include/config.h ||
            cp -f $SRS_PATCH_SOURCE ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit/crypto/include/config.h
        fi &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/libsrtp-2-fit install &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srtp2 ${SRS_OBJS}/ &&
        echo "The libsrtp-2-fit is ok."
    fi
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build libsrtp failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# libopus, for WebRTC to transcode AAC with Opus.
#####################################################################################
# For cross build, we use opus of FFmpeg, so we don't build the libopus.
if [[ $SRS_RTC == YES && $SRS_USE_SYS_FFMPEG != YES && $SRS_FFMPEG_OPUS != YES ]]; then
    # Only build static libraries if no shared FFmpeg.
    if [[ $SRS_SHARED_FFMPEG != YES ]]; then
        OPUS_OPTIONS="--disable-shared --disable-doc"
    fi
    if [[ $OS_IS_LOONGARCH64 == YES ]]; then
        OPUS_OPTIONS="$OPUS_OPTIONS --build=loongarch64-unknown-linux-gnu"
    fi
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/opus/lib/libopus.a ]]; then
        rm -rf ${SRS_OBJS}/opus && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/opus ${SRS_OBJS}/ &&
        echo "The opus-1.3.1 is ok."
    else
        echo "Building opus-1.3.1." &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/opus ${SRS_OBJS}/opus &&
        tar xf ${SRS_WORKDIR}/3rdparty/opus-1.3.1.tar.gz -C ${SRS_OBJS}/${SRS_PLATFORM} &&
        (
            # Opus requires automake 1.15, and fails for automake 1.16+, so we run autoreconf to fix it.
            cd ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 && autoreconf &&
            ./configure --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/opus --enable-static $OPUS_OPTIONS
        ) &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/opus-1.3.1 install &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/opus ${SRS_OBJS}/ &&
        echo "The opus-1.3.1 is ok."
    fi
    if [ ! -f ${SRS_OBJS}/opus/lib/libopus.a ]; then echo "Build opus-1.3.1 failed."; exit -1; fi
fi

#####################################################################################
# ffmpeg-fit, for WebRTC to transcode AAC with Opus.
#####################################################################################
if [[ $SRS_FFMPEG_FIT == YES && $SRS_USE_SYS_FFMPEG == YES ]]; then
    echo "Warning: Use system ffmpeg, without compiling ffmpeg."
fi
if [[ $SRS_FFMPEG_FIT == YES && $SRS_USE_SYS_FFMPEG == NO ]]; then
    FFMPEG_CONFIGURE="env SRS_FFMPEG_FIT=on"
    if [[ $SRS_FFMPEG_OPUS != YES ]]; then
        FFMPEG_CONFIGURE="$FFMPEG_CONFIGURE PKG_CONFIG_PATH=${SRS_DEPENDS_LIBS}/opus/lib/pkgconfig"
    fi
    FFMPEG_CONFIGURE="$FFMPEG_CONFIGURE ./configure"

    # Disable all features, note that there are still some options need to be disabled.
    FFMPEG_OPTIONS="--disable-everything"
    # Disable all asm for FFmpeg, to compatible with ARM CPU.
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-asm --disable-x86asm --disable-inline-asm"
    # Only build static libraries if no shared FFmpeg.
    if [[ $SRS_SHARED_FFMPEG == YES ]]; then
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-shared"
    fi
    # For loongson/mips64, disable mips64r6, or build failed.
    if [[ $OS_IS_MIPS64 == YES && $OS_IS_LOONGSON == YES ]]; then FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-mips64r6"; fi
    # For cross-build.
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-cross-compile --target-os=linux --disable-pthreads"
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --arch=$SRS_CROSS_BUILD_ARCH";
        if [[ $SRS_CROSS_BUILD_CPU != "" ]]; then FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cpu=$SRS_CROSS_BUILD_CPU"; fi
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cross-prefix=$SRS_CROSS_BUILD_PREFIX"
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --cc=${SRS_TOOL_CC} --cxx=${SRS_TOOL_CXX} --ar=${SRS_TOOL_AR} --ld=${SRS_TOOL_LD}"
    fi
    # For audio codec opus, use FFmpeg native one, or external libopus.
    if [[ $SRS_FFMPEG_OPUS == YES ]]; then
        # TODO: FIXME: Note that the audio might be corrupted, see https://github.com/ossrs/srs/issues/3140
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=opus --enable-encoder=opus"
    else
        FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=libopus --enable-encoder=libopus --enable-libopus"
    fi
    # Disable features of ffmpeg.
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-avdevice --disable-avformat --disable-swscale --disable-postproc --disable-avfilter --disable-network"
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-dwt --disable-error-resilience --disable-lsp --disable-lzo --disable-faan --disable-pixelutils"
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-hwaccels --disable-devices --disable-audiotoolbox --disable-videotoolbox --disable-cuvid"
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-d3d11va --disable-dxva2 --disable-ffnvcodec --disable-nvdec --disable-nvenc --disable-v4l2-m2m --disable-vaapi"
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-vdpau --disable-appkit --disable-coreimage --disable-avfoundation --disable-securetransport --disable-iconv"
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --disable-lzma --disable-sdl2"
    # Enable FFmpeg native AAC encoder and decoder.
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=aac --enable-decoder=aac_fixed --enable-decoder=aac_latm --enable-encoder=aac"
    # Enable FFmpeg native MP3 decoder, which depends on dct.
    FFMPEG_OPTIONS="$FFMPEG_OPTIONS --enable-decoder=mp3 --enable-dct"

    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/ffmpeg/lib/libavcodec.a ]]; then
        rm -rf ${SRS_OBJS}/ffmpeg && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/ffmpeg ${SRS_OBJS}/ &&
        echo "The ffmpeg-4-fit is ok."
    else
        echo "Building ffmpeg-4-fit." &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/ffmpeg \
            ${SRS_OBJS}/ffmpeg &&
        cp -rf ${SRS_WORKDIR}/3rdparty/ffmpeg-4-fit ${SRS_OBJS}/${SRS_PLATFORM}/ &&
        (
            cd ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit &&
            $FFMPEG_CONFIGURE --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/ffmpeg \
                --pkg-config=pkg-config --pkg-config-flags='--static' --extra-libs='-lpthread' --extra-libs='-lm' \
                ${FFMPEG_OPTIONS}
        ) &&
        # See https://www.laoyuyu.me/2019/05/23/android/clang_compile_ffmpeg/
        if [[ $SRS_CROSS_BUILD == YES ]]; then
          sed -i -e 's/#define getenv(x) NULL/\/\*#define getenv(x) NULL\*\//g' ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
          sed -i -e 's/#define HAVE_GMTIME_R 0/#define HAVE_GMTIME_R 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
          sed -i -e 's/#define HAVE_LOCALTIME_R 0/#define HAVE_LOCALTIME_R 1/g' ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
          # For MIPS, which fail with:
          #     ./libavutil/libm.h:54:32: error: static declaration of 'cbrt' follows non-static declaration
          #     /root/openwrt/staging_dir/toolchain-mipsel_24kc_gcc-8.4.0_musl/include/math.h:163:13: note: previous declaration of 'cbrt' was here
          if [[ $SRS_CROSS_BUILD_ARCH == "mipsel" || $SRS_CROSS_BUILD_ARCH == "arm" || $SRS_CROSS_BUILD_ARCH == "aarch64" ]]; then
            sed -i -e 's/#define HAVE_CBRT 0/#define HAVE_CBRT 1/g'         ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_CBRTF 0/#define HAVE_CBRTF 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_COPYSIGN 0/#define HAVE_COPYSIGN 1/g' ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_ERF 0/#define HAVE_ERF 1/g'           ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_HYPOT 0/#define HAVE_HYPOT 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_RINT 0/#define HAVE_RINT 1/g'         ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_LRINT 0/#define HAVE_LRINT 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_LRINTF 0/#define HAVE_LRINTF 1/g'     ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_ROUND 0/#define HAVE_ROUND 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_ROUNDF 0/#define HAVE_ROUNDF 1/g'     ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_TRUNC 0/#define HAVE_TRUNC 1/g'       ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            sed -i -e 's/#define HAVE_TRUNCF 0/#define HAVE_TRUNCF 1/g'     ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit/config.h &&
            echo "FFmpeg sed ok"
          fi
        fi &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/ffmpeg-4-fit install &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/ffmpeg ${SRS_OBJS}/ &&
        echo "The ffmpeg-4-fit is ok."
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-4-fit failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# live transcoding, ffmpeg-4.1, x264-core157, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
# Guess where is the ffmpeg.
SYSTEMP_FFMPEG_BIN=`which ffmpeg`
# Always link the ffmpeg tools if exists.
if [[ -f $SYSTEMP_FFMPEG_BIN && ! -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
    mkdir -p ${SRS_OBJS}/ffmpeg/bin &&
    cp -f $SYSTEMP_FFMPEG_BIN ${SRS_OBJS}/ffmpeg/bin/
fi
if [[ $SRS_FFMPEG_TOOL == YES ]]; then
    if [[ -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
        cp -f $SYSTEMP_FFMPEG_BIN ${SRS_OBJS}/ffmpeg/bin/ &&
        echo "ffmpeg-4.1 is ok.";
    else
        echo -e "${RED}Error: No FFmpeg found at /usr/local/bin/ffmpeg${BLACK}"
        echo -e "${RED}    Please copy it from srs-docker${BLACK}"
        echo -e "${RED}    or download from http://ffmpeg.org/download.html${BLACK}"
        echo -e "${RED}    or disable it by --without-ffmpeg${BLACK}"
        exit -1;
    fi
fi

#####################################################################################
# SRT module, https://github.com/ossrs/srs/issues/1147#issuecomment-577469119
#####################################################################################
if [[ $SRS_SRT == YES && $SRS_USE_SYS_SRT == YES ]]; then
    echo "Warning: Use system libsrt, without compiling srt."
fi
if [[ $SRS_SRT == YES && $SRS_USE_SYS_SRT == NO ]]; then
    # Always disable c++11 for libsrt, because only the srt-app requres it.
    LIBSRT_OPTIONS="--enable-apps=0  --enable-static=1 --enable-c++11=0"
    if [[ $SRS_SHARED_SRT == YES ]]; then
        LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=1"
    else
        LIBSRT_OPTIONS="$LIBSRT_OPTIONS --enable-shared=0"
    fi
    # For windows build, over cygwin
    if [[ $SRS_CYGWIN64 == YES ]]; then
        LIBSRT_OPTIONS="$LIBSRT_OPTIONS --cygwin-use-posix"
    fi
    # For cross-build.
    if [[ $SRS_CROSS_BUILD == YES ]]; then
        TOOL_GCC_REALPATH=$(realpath $(which $SRS_TOOL_CC))
        SRT_COMPILER_PREFIX=$(echo $TOOL_GCC_REALPATH |sed 's/-gcc.*$/-/')
        LIBSRT_OPTIONS="$LIBSRT_OPTIONS --with-compiler-prefix=$SRT_COMPILER_PREFIX"
    fi

    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt/lib/libsrt.a ]]; then
        rm -rf ${SRS_OBJS}/srt && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt ${SRS_OBJS}/ &&
        echo "libsrt-1-fit is ok."
    else
        if [[ $SRS_USE_SYS_SSL != YES && ! -d ${SRS_OBJS}/openssl/lib/pkgconfig ]]; then
            echo "OpenSSL pkgconfig no found, build srt-1-fit failed."
            exit -1
        fi
        echo "Build srt-1-fit" &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt ${SRS_OBJS}/srt &&
        cp -rf ${SRS_WORKDIR}/3rdparty/srt-1-fit ${SRS_OBJS}/${SRS_PLATFORM}/ &&
        patch -p0 -R ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit/srtcore/api.cpp ${SRS_WORKDIR}/3rdparty/patches/srt/api.cpp-01.patch &&
        (
            cd ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit &&
            env PKG_CONFIG_PATH=${SRS_DEPENDS_LIBS}/openssl/lib/pkgconfig \
                ./configure --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/srt $LIBSRT_OPTIONS
        ) &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/srt-1-fit install &&
        # If exists lib64 of libsrt, copy it to lib
        if [[ -d ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt/lib64 ]]; then
            cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt/lib64 ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt/lib
        fi &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/srt ${SRS_OBJS}/ &&
        echo "libsrt-1-fit is ok."
    fi
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build srt-1-fit failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# build utest code
#####################################################################################
if [[ $SRS_UTEST == YES ]]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gtest/googletest/include/gtest/gtest.h ]]; then
        rm -rf ${SRS_OBJS}/gtest && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gtest ${SRS_OBJS}/ &&
        echo "The gtest-fit is ok."
    else
        echo "Build gtest-fit" &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}gtest-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gtest ${SRS_OBJS}/gtest &&
        cp -rf ${SRS_WORKDIR}/3rdparty/gtest-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gtest &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gtest ${SRS_OBJS}/ &&
        echo "The gtest-fit is ok."
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gtest-1.6.0 failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# build gperf code
#####################################################################################
if [[ $SRS_GPERF == YES ]]; then
    if [[ -f ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gperf/bin/pprof ]]; then
        rm -rf ${SRS_OBJS}/gperf && cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gperf ${SRS_OBJS}/ &&
        cp -f ${SRS_OBJS}/gperf/bin/pprof ${SRS_OBJS}/ &&
        echo "The gperftools-2-fit is ok."
    else
        echo "Build gperftools-2-fit" &&
        rm -rf ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2-fit ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gperf \
            ${SRS_OBJS}/gperf ${SRS_OBJS}/pprof &&
        cp -rf ${SRS_WORKDIR}/3rdparty/gperftools-2-fit ${SRS_OBJS}/${SRS_PLATFORM}/ &&
        (
            cd ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2-fit &&
            ./configure --prefix=${SRS_DEPENDS_LIBS}/${SRS_PLATFORM}/3rdparty/gperf --enable-frame-pointers
        ) &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2-fit ${SRS_JOBS} &&
        make -C ${SRS_OBJS}/${SRS_PLATFORM}/gperftools-2-fit install &&
        cp -rf ${SRS_OBJS}/${SRS_PLATFORM}/3rdparty/gperf ${SRS_OBJS}/ &&
        cp -f ${SRS_OBJS}/gperf/bin/pprof ${SRS_OBJS}/ &&
        echo "The gperftools-2-fit is ok."
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build gperftools-2-fit failed, ret=$ret"; exit $ret; fi
fi
