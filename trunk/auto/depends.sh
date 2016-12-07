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
    
    # cross build for arm, install the cross build tool chain.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        $SrsArmCC --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing gcc-arm-linux-gnueabi g++-arm-linux-gnueabi."
            require_sudoer "sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi"
            sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The gcc-arm-linux-gnueabi g++-arm-linux-gnueabi are installed."
        fi
    fi
    
    # cross build for mips, user must installed the tool chain.
    if [ $SRS_MIPS_UBUNTU12 = YES ]; then
        $SrsArmCC --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "You must install the tool chain: $SrsArmCC"
            return 2
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

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/include/pcre.h ]]; then
            echo "Installing libpcre3-dev."
            require_sudoer "sudo apt-get install -y --force-yes libpcre3-dev"
            sudo apt-get install -y --force-yes libpcre3-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The libpcre3-dev is installed."
        fi
    fi
    
    if [ $SRS_FFMPEG_TOOL = YES ]; then
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing autoconf."
            require_sudoer "sudo apt-get install -y --force-yes autoconf"
            sudo apt-get install -y --force-yes autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The autoconf is installed."
        fi
        
        libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing libtool."
            require_sudoer "sudo apt-get install -y --force-yes libtool"
            sudo apt-get install -y --force-yes libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The libtool is installed."
        fi
        
        if [[ ! -f /usr/include/zlib.h ]]; then
            echo "Installing zlib1g-dev."
            require_sudoer "sudo apt-get install -y --force-yes zlib1g-dev"
            sudo apt-get install -y --force-yes zlib1g-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The zlib1g-dev is installed."
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
    
    # cross build for arm, install the cross build tool chain.
    if [ $SRS_CROSS_BUILD = YES ]; then
        echo "CentOS doesn't support crossbuild for arm/mips, please use Ubuntu instead."
        return 1
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

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/include/pcre.h ]]; then
            echo "Installing pcre-devel."
            require_sudoer "sudo yum install -y pcre-devel"
            sudo yum install -y pcre-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The pcre-devel is installed."
        fi
    fi
    
    if [ $SRS_FFMPEG_TOOL = YES ]; then
        automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing automake."
            require_sudoer "sudo yum install -y automake"
            sudo yum install -y automake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The automake is installed."
        fi
        
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing autoconf."
            require_sudoer "sudo yum install -y autoconf"
            sudo yum install -y autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The autoconf is installed."
        fi
        
        libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing libtool."
            require_sudoer "sudo yum install -y libtool"
            sudo yum install -y libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The libtool is installed."
        fi
        
        if [[ ! -f /usr/include/zlib.h ]]; then
            echo "Installing zlib-devel."
            require_sudoer "sudo yum install -y zlib-devel"
            sudo yum install -y zlib-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The zlib-devel is installed."
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
# for Centos, auto install tools by yum
#####################################################################################
OS_IS_OSX=NO
function OSX_prepare()
{
    uname -s|grep Darwin >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        if [ $SRS_OSX = YES ]; then
            echo "Current OS `uname -s` is not OSX, please check your configure options."
            exit 1;
        fi
        return 0;
    fi
    
    # cross build for arm, install the cross build tool chain.
    if [ $SRS_CROSS_BUILD = YES ]; then
        echo "OSX doesn't support crossbuild for arm/mips, please use Ubuntu instead."
        return 1
    fi

    OS_IS_OSX=YES
    echo "Installing tools for OSX."
    # requires the osx when os
    if [ $OS_IS_OSX = YES ]; then
        if [ $SRS_OSX = NO ]; then
            echo "Invalid configure options for OSX, please specify --osx."
            exit 1
        fi
    fi

    brew --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing brew."
        echo "ruby -e \"$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)\""
        ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The brew is installed."
    fi
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc."
        echo "brew install gcc"
        brew install gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc is installed."
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing gcc-c++."
        echo "brew install gcc-c++"
        brew install gcc-c++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The gcc-c++ is installed."
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing make."
        echo "brew install make"
        brew install make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The make is installed."
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing patch."
        echo "brew install patch"
        brew install patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The patch is installed."
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "Installing unzip."
        echo "brew install unzip"
        brew install unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The unzip is installed."
    fi

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/local/include/pcre.h ]]; then
        echo "Installing pcre."
        echo "brew install pcre"
        brew install pcre; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "The pcre is installed."
        fi
    fi

    if [ $SRS_FFMPEG_TOOL = YES ]; then
        automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing automake."
            echo "brew install automake"
            brew install automake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The automake is installed."
        fi
        
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing autoconf."
            echo "brew install autoconf"
            brew install autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The autoconf is installed."
        fi
        
        which libtool >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing libtool."
            echo "brew install libtool"
            brew install libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The libtool is installed."
        fi

        brew info zlib >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "Installing zlib."
            echo "brew install zlib"
            brew install zlib; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "The zlib is installed."
        fi
    fi
    
    echo "Tools for OSX are installed."
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    OSX_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Install tools for OSX failed, ret=$ret"; exit $ret; fi
fi

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
# user must specifies something what a fuck, we suppport following os:
#       centos/ubuntu/osx,
#       cross build for embeded system, for example, mips or arm,
#       directly build on arm/mips, for example, pi or cubie,
#       export srs-librtmp
# others is invalid.
if [[ $OS_IS_UBUNTU = NO && $OS_IS_CENTOS = NO && $OS_IS_OSX = NO && $SRS_EXPORT_LIBRTMP_PROJECT = NO ]]; then
    if [[ $SRS_PI = NO && $SRS_CUBIE = NO && $SRS_CROSS_BUILD = NO ]]; then
        echo "What a fuck, your OS `uname -s` is not supported."
        exit 1
    fi
fi

#####################################################################################
# st-1.9
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # check the cross build flag file, if flag changed, need to rebuild the st.
    _ST_MAKE=linux-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_EPOLL"
    # for osx, use darwin for st, donot use epoll.
    if [ $OS_IS_OSX = YES ]; then
        _ST_MAKE=darwin-debug && _ST_EXTRA_CFLAGS="-DMD_HAVE_KQUEUE"
    fi
    # memory leak for linux-optimized
    # @see: https://github.com/ossrs/srs/issues/197
    if [ $SRS_CROSS_BUILD = YES ]; then
        # ok, arm specified, if the flag filed does not exists, need to rebuild.
        if [[ -f ${SRS_OBJS}/_flag.st.cross.build.tmp && -f ${SRS_OBJS}/st/libst.a ]]; then
            echo "The st-1.9t for arm is ok.";
        else
            # TODO: FIXME: patch the bug.
            # patch st for arm, @see: https://github.com/ossrs/srs/wiki/v1_CN_SrsLinuxArm#st-arm-bug-fix
            echo "Building st-1.9t for arm.";
            (
                rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
                unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && chmod +w * &&
                patch -p0 < ../../3rdparty/patches/1.st.arm.patch &&
                patch -p0 < ../../3rdparty/patches/3.st.osx.kqueue.patch &&
                patch -p0 < ../../3rdparty/patches/4.st.disable.examples.patch &&
                make ${_ST_MAKE} CC=${SrsArmCC} AR=${SrsArmAR} LD=${SrsArmLD} RANDLIB=${SrsArmRANDLIB} EXTRA_CFLAGS="${_ST_EXTRA_CFLAGS}" &&
                cd .. && rm -rf st && ln -sf st-1.9/obj st &&
                cd .. && touch ${SRS_OBJS}/_flag.st.cross.build.tmp
            )
        fi
    else
        if [[ ! -f ${SRS_OBJS}/_flag.st.cross.build.tmp && -f ${SRS_OBJS}/st/libst.a ]]; then
            echo "The st-1.9t is ok.";
        else
            # patch st for arm, @see: https://github.com/ossrs/srs/wiki/v1_CN_SrsLinuxArm#st-arm-bug-fix
            echo "Building st-1.9t.";
            (
                rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
                unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && chmod +w * &&
                patch -p0 < ../../3rdparty/patches/1.st.arm.patch &&
                patch -p0 < ../../3rdparty/patches/3.st.osx.kqueue.patch &&
                patch -p0 < ../../3rdparty/patches/4.st.disable.examples.patch &&
                make ${_ST_MAKE} EXTRA_CFLAGS="${_ST_EXTRA_CFLAGS}" &&
                cd .. && rm -rf st && ln -sf st-1.9/obj st &&
                cd .. && rm -f ${SRS_OBJS}/_flag.st.cross.build.tmp
            )
        fi
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build st-1.9 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "Build st-1.9 static lib failed."; exit -1; fi
fi

#####################################################################################
# http-parser-2.1
#####################################################################################
# check the cross build flag file, if flag changed, need to rebuild the st.
if [ $SRS_HTTP_CORE = YES ]; then
    echo "The http-parser is copied into srs_http_stack.*pp"
fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
function write_nginx_html5()
{
    cat<<END > ${html_file}
<video "autoplay" "controls" "autobuffer" type="application/vnd.apple.mpegurl"
    src="${hls_stream}">
</video>
END
}
# create the nginx dir, for http-server if not build nginx
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    mkdir -p ${SRS_OBJS}/nginx
fi
# make nginx
__SRS_BUILD_NGINX=NO; if [ $SRS_CROSS_BUILD = NO ]; then if [ $SRS_NGINX = YES ]; then __SRS_BUILD_NGINX=YES; fi fi
if [ $__SRS_BUILD_NGINX = YES ]; then
    if [[ -f ${SRS_OBJS}/nginx/sbin/nginx ]]; then
        echo "The nginx-1.5.7 is ok.";
    else
        echo "Building nginx-1.5.7";
        (
            rm -rf ${SRS_OBJS}/nginx-1.5.7 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/nginx-1.5.7.zip && cd nginx-1.5.7 && 
            ./configure --prefix=`pwd`/_release && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf nginx && ln -sf nginx-1.5.7/_release nginx
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build nginx-1.5.7 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/nginx/sbin/nginx ]; then echo "Build nginx-1.5.7 failed."; exit -1; fi

    # use current user to config nginx,
    # srs will write ts/m3u8 file use current user,
    # nginx default use nobody, so cannot read the ts/m3u8 created by srs.
    cp ${SRS_OBJS}/nginx/conf/nginx.conf ${SRS_OBJS}/nginx/conf/nginx.conf.bk
    $SED "s/^.user  nobody;/user `whoami`;/g" ${SRS_OBJS}/nginx/conf/nginx.conf
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
if [ $SRS_HTTP_CALLBACK = YES ]; then
    if [[ -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        require_sudoer "install CherryPy-3.2.4"
        echo "Installing CherryPy-3.2.4";
        (
            sudo rm -rf ${SRS_OBJS}/CherryPy-3.2.4 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/CherryPy-3.2.4.zip && cd CherryPy-3.2.4 && 
            sudo python setup.py install
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build CherryPy-3.2.4 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]; then echo "build CherryPy-3.2.4 failed."; exit -1; fi
fi

if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
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
# generate demo index.html
#####################################################################################
# if nginx enalbed, generate nginx index file.
if [ $__SRS_BUILD_NGINX = YES ]; then
    rm -f ${SRS_OBJS}/nginx/html/index.html &&
    ln -sf `pwd`/research/players/nginx_index.html ${SRS_OBJS}/nginx/html/index.html
fi
# if http-server enalbed, use srs embeded http-server
if [ $SRS_HTTP_SERVER = YES ]; then
    rm -f ${SRS_OBJS}/nginx/html/index.html &&
    ln -sf `pwd`/research/players/srs-http-server_index.html ${SRS_OBJS}/nginx/html/index.html
fi
# if api-server enabled, generate for api server.
if [ $SRS_HTTP_CALLBACK = YES ]; then
    rm -f ${SRS_OBJS}/nginx/html/index.html &&
    ln -sf `pwd`/research/players/api-server_index.html ${SRS_OBJS}/nginx/html/index.html
fi

#####################################################################################
# openssl, for rtmp complex handshake
#####################################################################################
# extra configure options
CONFIGURE_TOOL="./config"
if [ $SRS_CROSS_BUILD = YES ]; then
    CONFIGURE_TOOL="./Configure linux-armv4"
fi
if [ $SRS_OSX = YES ]; then
    CONFIGURE_TOOL="./Configure darwin64-`uname -m`-cc"
fi
OPENSSL_HOTFIX="-DOPENSSL_NO_HEARTBEATS"
# @see http://www.openssl.org/news/secadv/20140407.txt
# Affected users should upgrade to OpenSSL 1.0.1g. Users unable to immediately
# upgrade can alternatively recompile OpenSSL with -DOPENSSL_NO_HEARTBEATS.
if [ $SRS_SSL = YES ]; then
    if [ $SRS_USE_SYS_SSL = YES ]; then
        echo "Warning: Use system libssl, without compiling openssl."
    else
        # check the cross build flag file, if flag changed, need to rebuild the st.
        if [ $SRS_CROSS_BUILD = YES ]; then
            # ok, arm specified, if the flag filed does not exists, need to rebuild.
            if [[ -f ${SRS_OBJS}/_flag.ssl.cross.build.tmp && -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
                echo "The openssl-1.0.1f for arm is ok.";
            else
                echo "Building openssl-1.0.1f for ARM.";
                (
                    rm -rf ${SRS_OBJS}/openssl-1.0.1f && cd ${SRS_OBJS} && 
                    unzip -q ../3rdparty/openssl-1.0.1f.zip && cd openssl-1.0.1f && 
                    $CONFIGURE_TOOL --prefix=`pwd`/_release -no-shared no-asm $OPENSSL_HOTFIX &&
                    make CC=${SrsArmCC} GCC=${SrsArmGCC} AR="${SrsArmAR} r" \
                        LD=${SrsArmLD} LINK=${SrsArmGCC} RANDLIB=${SrsArmRANDLIB} && 
                    make install_sw &&
                    cd .. && rm -rf openssl && ln -sf openssl-1.0.1f/_release openssl &&
                    cd .. && touch ${SRS_OBJS}/_flag.ssl.cross.build.tmp
                )
            fi
        else
            # cross build not specified, if exists flag, need to rebuild for no-arm platform.
            if [[ ! -f ${SRS_OBJS}/_flag.ssl.cross.build.tmp && -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
                echo "Openssl-1.0.1f is ok.";
            else
                echo "Building openssl-1.0.1f.";
                (
                    rm -rf ${SRS_OBJS}/openssl-1.0.1f && cd ${SRS_OBJS} && 
                    unzip -q ../3rdparty/openssl-1.0.1f.zip && cd openssl-1.0.1f && 
                    $CONFIGURE_TOOL --prefix=`pwd`/_release -no-shared $OPENSSL_HOTFIX &&
                    make && make install_sw &&
                    cd .. && rm -rf openssl && ln -sf openssl-1.0.1f/_release openssl &&
                    cd .. && rm -f ${SRS_OBJS}/_flag.ssl.cross.build.tmp
                )
            fi
        fi
        # check status
        ret=$?; if [[ $ret -ne 0 ]]; then echo "Build openssl-1.0.1f failed, ret=$ret"; exit $ret; fi
        if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "Build openssl-1.0.1f failed."; exit -1; fi
    fi
fi

#####################################################################################
# live transcoding, ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
if [ $SRS_FFMPEG_TOOL = YES ]; then
    if [[ -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
        echo "The ffmpeg-2.1 is ok.";
    else
        echo "Building ffmpeg-2.1.";
        (
            cd ${SRS_OBJS} && pwd_dir=`pwd` && 
            rm -rf ffmepg.src && mkdir -p ffmpeg.src && cd ffmpeg.src &&
            rm -f build_ffmpeg.sh && ln -sf ../../auto/build_ffmpeg.sh && . build_ffmpeg.sh &&
            cd ${pwd_dir} && rm -rf ffmpeg && ln -sf ffmpeg.src/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "Build ffmpeg-2.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]; then echo "Build ffmpeg-2.1 failed."; exit -1; fi
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

if [ $SRS_LIBRTMP = YES ]; then
    mkdir -p ${SRS_OBJS}/research
    
    # librtmp
    (cd ${SRS_WORKDIR}/research/librtmp && mkdir -p objs && ln -sf `pwd`/objs ../../${SRS_OBJS_DIR}/research/librtmp)
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

