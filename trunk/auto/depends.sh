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
echo "check gcc/g++/gdb/make"
echo "depends tools are ok"
#####################################################################################
# for Ubuntu, auto install tools by apt-get
#####################################################################################
OS_IS_UBUNTU=NO
function Ubuntu_prepare()
{
    if [ $SRS_CUBIE = YES ]; then
        echo "for cubieboard, use ubuntu prepare"
    else
        uname -v|grep Ubuntu >/dev/null 2>&1
        ret=$?; if [[ 0 -ne $ret ]]; then
            return 0;
        fi
    fi
    
    # for arm, install the cross build tool chain.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        $SrsArmCC --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi"
            require_sudoer "sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi"
            sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi success"
        fi
    fi
    
    # for mips, user must installed the tool chain.
    if [ $SRS_MIPS_UBUNTU12 = YES ]; then
        $SrsArmCC --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "user must install the tool chain: $SrsArmCC"
            return 2
        fi
    fi

    OS_IS_UBUNTU=YES
    echo "Ubuntu detected, install tools if needed"
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc"
        require_sudoer "sudo apt-get install -y --force-yes gcc"
        sudo apt-get install -y --force-yes gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install gcc success"
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install g++"
        require_sudoer "sudo apt-get install -y --force-yes g++"
        sudo apt-get install -y --force-yes g++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install g++ success"
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install make"
        require_sudoer "sudo apt-get install -y --force-yes make"
        sudo apt-get install -y --force-yes make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install make success"
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install patch"
        require_sudoer "sudo apt-get install -y --force-yes patch"
        sudo apt-get install -y --force-yes patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install patch success"
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install unzip"
        require_sudoer "sudo apt-get install -y --force-yes unzip"
        sudo apt-get install -y --force-yes unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install unzip success"
    fi

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/include/pcre.h ]]; then
            echo "install libpcre3-dev"
            require_sudoer "sudo apt-get install -y --force-yes libpcre3-dev"
            sudo apt-get install -y --force-yes libpcre3-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install libpcre3-dev success"
        fi
    fi
    
    if [ $SRS_FFMPEG_TOOL = YES ]; then
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install autoconf"
            require_sudoer "sudo apt-get install -y --force-yes autoconf"
            sudo apt-get install -y --force-yes autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install autoconf success"
        fi
        
        libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install libtool"
            require_sudoer "sudo apt-get install -y --force-yes libtool"
            sudo apt-get install -y --force-yes libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install libtool success"
        fi
        
        if [[ ! -f /usr/include/zlib.h ]]; then
            echo "install zlib1g-dev"
            require_sudoer "sudo apt-get install -y --force-yes zlib1g-dev"
            sudo apt-get install -y --force-yes zlib1g-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install zlib1g-dev success"
        fi
    fi
    
    echo "Ubuntu install tools success"
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    Ubuntu_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Ubuntu prepare failed, ret=$ret"; exit $ret; fi
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
    
    # for arm, install the cross build tool chain.
    if [ $SRS_EMBEDED_CPU = YES ]; then
        echo "embeded(arm/mips) is invalid for CentOS"
        return 1
    fi

    OS_IS_CENTOS=YES
    echo "Centos detected, install tools if needed"
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc"
        require_sudoer "sudo yum install -y gcc"
        sudo yum install -y gcc; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install gcc success"
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc-c++"
        require_sudoer "sudo yum install -y gcc-c++"
        sudo yum install -y gcc-c++; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install gcc-c++ success"
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install make"
        require_sudoer "sudo yum install -y make"
        sudo yum install -y make; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install make success"
    fi
    
    patch --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install patch"
        require_sudoer "sudo yum install -y patch"
        sudo yum install -y patch; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install patch success"
    fi
    
    unzip --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install unzip"
        require_sudoer "sudo yum install -y unzip"
        sudo yum install -y unzip; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install unzip success"
    fi

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/include/pcre.h ]]; then
            echo "install pcre-devel"
            require_sudoer "sudo yum install -y pcre-devel"
            sudo yum install -y pcre-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install pcre-devel success"
        fi
    fi
    
    if [ $SRS_FFMPEG_TOOL = YES ]; then
        automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install automake"
            require_sudoer "sudo yum install -y automake"
            sudo yum install -y automake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install automake success"
        fi
        
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install autoconf"
            require_sudoer "sudo yum install -y autoconf"
            sudo yum install -y autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install autoconf success"
        fi
        
        libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install libtool"
            require_sudoer "sudo yum install -y libtool"
            sudo yum install -y libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install libtool success"
        fi
        
        if [[ ! -f /usr/include/zlib.h ]]; then
            echo "install zlib-devel"
            require_sudoer "sudo yum install -y zlib-devel"
            sudo yum install -y zlib-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install zlib-devel success"
        fi
    fi
    
    echo "Centos install tools success"
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    Centos_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "CentOS prepare failed, ret=$ret"; exit $ret; fi
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
            echo "OSX check failed, actual is `uname -s`"
            exit 1;
        fi
        return 0;
    fi
    
    # for arm, install the cross build tool chain.
    if [ $SRS_EMBEDED_CPU = YES ]; then
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

    if [ $SRS_NGINX = YES ]; then
        if [[ ! -f /usr/local/include/pcre.h ]]; then
        echo "install pcre"
        echo "brew install pcre"
        brew install pcre; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install pcre success"
        fi
    fi

    if [ $SRS_FFMPEG_TOOL = YES ]; then
        automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install automake"
            echo "brew install automake"
            brew install automake; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install automake success"
        fi
        
        autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install autoconf"
            echo "brew install autoconf"
            brew install autoconf; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install autoconf success"
        fi
        
        which libtool >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install libtool"
            echo "brew install libtool"
            brew install libtool; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install libtool success"
        fi

        brew info zlib >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install zlib"
            echo "brew install zlib"
            brew install zlib; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install zlib success"
        fi
    fi
    
    echo "OSX install tools success"
    return 0
}
# donot prepare tools, for srs-librtmp depends only gcc and g++.
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    OSX_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "OSX prepare failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# check the os.
#####################################################################################
# user must specifies something what a fuck, we suppport following os:
#       centos/ubuntu/osx,
#       embeded system, for example, mips or arm,
#       export srs-librtmp
# others is invalid.
if [[ $OS_IS_UBUNTU = NO && $OS_IS_CENTOS = NO && $OS_IS_OSX = NO && $SRS_EMBEDED_CPU = NO && $SRS_EXPORT_LIBRTMP_PROJECT = NO ]]; then
    echo "what a fuck, os not supported."
    exit 1
fi

#####################################################################################
# st-1.9
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    # check the arm flag file, if flag changed, need to rebuild the st.
    _ST_MAKE=linux-debug && _ST_EXTRA_CFLAGS="EXTRA_CFLAGS=-DMD_HAVE_EPOLL"
    # for osx, use darwin for st, donot use epoll.
    if [ $OS_IS_OSX = YES ]; then
        _ST_MAKE=darwin-debug && _ST_EXTRA_CFLAGS="EXTRA_CFLAGS=-DMD_HAVE_KQUEUE"
    fi
    # memory leak for linux-optimized
    # @see: https://github.com/simple-rtmp-server/srs/issues/197
    if [ $SRS_EMBEDED_CPU = YES ]; then
        # ok, arm specified, if the flag filed does not exists, need to rebuild.
        if [[ -f ${SRS_OBJS}/_flag.st.arm.tmp && -f ${SRS_OBJS}/st/libst.a ]]; then
            echo "st-1.9t for arm is ok.";
        else
            # TODO: FIXME: patch the bug.
            # patch st for arm, @see: https://github.com/simple-rtmp-server/srs/wiki/v1_CN_SrsLinuxArm#st-arm-bug-fix
            echo "build st-1.9t for arm"; 
            (
                rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
                unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && chmod +w * &&
                patch -p0 < ../../3rdparty/patches/1.st.arm.patch &&
                patch -p0 < ../../3rdparty/patches/3.st.osx.kqueue.patch &&
                patch -p0 < ../../3rdparty/patches/4.st.disable.examples.patch &&
                make ${_ST_MAKE} CC=${SrsArmCC} AR=${SrsArmAR} LD=${SrsArmLD} RANDLIB=${SrsArmRANDLIB} ${_ST_EXTRA_CFLAGS} &&
                cd .. && rm -rf st && ln -sf st-1.9/obj st &&
                cd .. && touch ${SRS_OBJS}/_flag.st.arm.tmp
            )
        fi
    else
        if [[ ! -f ${SRS_OBJS}/_flag.st.arm.tmp && -f ${SRS_OBJS}/st/libst.a ]]; then
            echo "st-1.9t is ok.";
        else
            echo "build st-1.9t"; 
            (
                rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
                unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && chmod +w * &&
                patch -p0 < ../../3rdparty/patches/3.st.osx.kqueue.patch &&
                patch -p0 < ../../3rdparty/patches/4.st.disable.examples.patch &&
                make ${_ST_MAKE} ${_ST_EXTRA_CFLAGS} &&
                cd .. && rm -rf st && ln -sf st-1.9/obj st &&
                cd .. && rm -f ${SRS_OBJS}/_flag.st.arm.tmp
            )
        fi
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build st-1.9 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "build st-1.9 static lib failed."; exit -1; fi
fi

#####################################################################################
# http-parser-2.1
#####################################################################################
# check the arm flag file, if flag changed, need to rebuild the st.
if [ $SRS_HTTP_PARSER = YES ]; then
    # ok, arm specified, if the flag filed does not exists, need to rebuild.
    if [ $SRS_EMBEDED_CPU = YES ]; then
        if [[ -f ${SRS_OBJS}/_flag.st.hp.tmp && -f ${SRS_OBJS}/hp/http_parser.h && -f ${SRS_OBJS}/hp/libhttp_parser.a ]]; then
            echo "http-parser-2.1 for arm is ok.";
        else
            echo "build http-parser-2.1 for arm";
            (
                rm -rf ${SRS_OBJS}/http-parser-2.1 && cd ${SRS_OBJS} && unzip -q ../3rdparty/http-parser-2.1.zip && 
                cd http-parser-2.1 && 
                patch -p0 < ../../3rdparty/patches/2.http.parser.patch &&
                make CC=${SrsArmCC} AR=${SrsArmAR} package &&
                cd .. && rm -rf hp && ln -sf http-parser-2.1 hp &&
                cd .. && touch ${SRS_OBJS}/_flag.st.hp.tmp
            )
        fi
    else
        # arm not specified, if exists flag, need to rebuild for no-arm platform.
        if [[ ! -f ${SRS_OBJS}/_flag.st.hp.tmp && -f ${SRS_OBJS}/hp/http_parser.h && -f ${SRS_OBJS}/hp/libhttp_parser.a ]]; then
            echo "http-parser-2.1 is ok.";
        else
            echo "build http-parser-2.1";
            (
                rm -rf ${SRS_OBJS}/http-parser-2.1 && cd ${SRS_OBJS} && unzip -q ../3rdparty/http-parser-2.1.zip && 
                cd http-parser-2.1 && 
                patch -p0 < ../../3rdparty/patches/2.http.parser.patch &&
                make package &&
                cd .. && rm -rf hp && ln -sf http-parser-2.1 hp &&
                cd .. && rm -f ${SRS_OBJS}/_flag.st.hp.tmp
            )
        fi
    fi

    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build http-parser-2.1 failed, ret=$ret"; exit $ret; fi
    if [[ ! -f ${SRS_OBJS}/hp/http_parser.h ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
    if [[ ! -f ${SRS_OBJS}/hp/libhttp_parser.a ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
function write_nginx_html5()
{
    cat<<END > ${html_file}
<video width="640" height="360"
        autoplay controls autobuffer 
        src="${hls_stream}"
        type="application/vnd.apple.mpegurl">
</video>
END
}
# create the nginx dir, for http-server if not build nginx
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    mkdir -p ${SRS_OBJS}/nginx
fi
# make nginx
__SRS_BUILD_NGINX=NO; if [ $SRS_EMBEDED_CPU = NO ]; then if [ $SRS_NGINX = YES ]; then __SRS_BUILD_NGINX=YES; fi fi
if [ $__SRS_BUILD_NGINX = YES ]; then
    if [[ -f ${SRS_OBJS}/nginx/sbin/nginx ]]; then
        echo "nginx-1.5.7 is ok.";
    else
        echo "build nginx-1.5.7"; 
        (
            rm -rf ${SRS_OBJS}/nginx-1.5.7 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/nginx-1.5.7.zip && cd nginx-1.5.7 && 
            ./configure --prefix=`pwd`/_release && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf nginx && ln -sf nginx-1.5.7/_release nginx
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build nginx-1.5.7 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/nginx/sbin/nginx ]; then echo "build nginx-1.5.7 failed."; exit -1; fi

    # use current user to config nginx,
    # srs will write ts/m3u8 file use current user,
    # nginx default use nobody, so cannot read the ts/m3u8 created by srs.
    cp ${SRS_OBJS}/nginx/conf/nginx.conf ${SRS_OBJS}/nginx/conf/nginx.conf.bk
    if [ $OS_IS_OSX = YES ]; then
        sed -i '' "s/^.user  nobody;/user `whoami`;/g" ${SRS_OBJS}/nginx/conf/nginx.conf
    else
        sed -i "s/^.user  nobody;/user `whoami`;/g" ${SRS_OBJS}/nginx/conf/nginx.conf
    fi
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
    echo "nginx is ok" > ${SRS_OBJS}/nginx/html/nginx.html
fi

#####################################################################################
# cherrypy for http hooks callback, CherryPy-3.2.4
#####################################################################################
if [ $SRS_HTTP_CALLBACK = YES ]; then
    if [[ -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        require_sudoer "install CherryPy-3.2.4"
        echo "install CherryPy-3.2.4"; 
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
    echo "link players to cherrypy static-dir"
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
    ret=$?; if [[ $ret -ne 0 ]]; then echo "link players to cherrypy static-dir failed, ret=$ret"; exit $ret; fi
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
if [ $SRS_EMBEDED_CPU = YES ]; then
    CONFIGURE_TOOL="./Configure linux-armv4"
fi
if [ $SRS_OSX = YES ]; then
    CONFIGURE_TOOL="./Configure darwin64-`uname -m`-cc"
fi
# @see http://www.openssl.org/news/secadv_20140407.txt
# Affected users should upgrade to OpenSSL 1.0.1g. Users unable to immediately
# upgrade can alternatively recompile OpenSSL with -DOPENSSL_NO_HEARTBEATS.
if [ $SRS_SSL = YES ]; then
    if [ $SRS_USE_SYS_SSL = YES ]; then
        echo "warning: donot compile ssl, use system ssl"
    else
        # check the arm flag file, if flag changed, need to rebuild the st.
        if [ $SRS_EMBEDED_CPU = YES ]; then
            # ok, arm specified, if the flag filed does not exists, need to rebuild.
            if [[ -f ${SRS_OBJS}/_flag.ssl.arm.tmp && -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
                echo "openssl-1.0.1f for arm is ok.";
            else
                echo "build openssl-1.0.1f for arm"; 
                (
                    rm -rf ${SRS_OBJS}/openssl-1.0.1f && cd ${SRS_OBJS} && 
                    unzip -q ../3rdparty/openssl-1.0.1f.zip && cd openssl-1.0.1f && 
                    $CONFIGURE_TOOL --prefix=`pwd`/_release -no-shared no-asm &&
                    make CC=${SrsArmCC} GCC=${SrsArmGCC} AR="${SrsArmAR} r" \
                        LD=${SrsArmLD} LINK=${SrsArmGCC} RANDLIB=${SrsArmRANDLIB} && 
                    make install_sw &&
                    cd .. && rm -rf openssl && ln -sf openssl-1.0.1f/_release openssl &&
                    cd .. && touch ${SRS_OBJS}/_flag.ssl.arm.tmp
                )
            fi
        else
            # arm not specified, if exists flag, need to rebuild for no-arm platform.
            if [[ ! -f ${SRS_OBJS}/_flag.ssl.arm.tmp && -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
                echo "openssl-1.0.1f is ok.";
            else
                echo "build openssl-1.0.1f"; 
                (
                    rm -rf ${SRS_OBJS}/openssl-1.0.1f && cd ${SRS_OBJS} && 
                    unzip -q ../3rdparty/openssl-1.0.1f.zip && cd openssl-1.0.1f && 
                    $CONFIGURE_TOOL --prefix=`pwd`/_release -no-shared &&
                    make && make install_sw &&
                    cd .. && rm -rf openssl && ln -sf openssl-1.0.1f/_release openssl &&
                    cd .. && rm -f ${SRS_OBJS}/_flag.ssl.arm.tmp
                )
            fi
        fi
        # check status
        ret=$?; if [[ $ret -ne 0 ]]; then echo "build openssl-1.0.1f failed, ret=$ret"; exit $ret; fi
        if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "build openssl-1.0.1f failed."; exit -1; fi
    fi
fi

#####################################################################################
# live transcoding, ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
if [ $SRS_FFMPEG_TOOL = YES ]; then
    if [[ -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]]; then
        echo "ffmpeg-2.1 is ok.";
    else
        echo "build ffmpeg-2.1"; 
        (
            cd ${SRS_OBJS} && pwd_dir=`pwd` && 
            rm -rf ffmepg.src && mkdir -p ffmpeg.src && cd ffmpeg.src &&
            rm -f build_ffmpeg.sh && ln -sf ../../auto/build_ffmpeg.sh && . build_ffmpeg.sh &&
            cd ${pwd_dir} && rm -rf ffmpeg && ln -sf ffmpeg.src/_release ffmpeg
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build ffmpeg-2.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/ffmpeg/bin/ffmpeg ]; then echo "build ffmpeg-2.1 failed."; exit -1; fi
fi

#####################################################################################
# build research code, librtmp
#####################################################################################
if [ $SRS_EXPORT_LIBRTMP_PROJECT = NO ]; then
    if [ $SRS_RESEARCH = YES ]; then
        mkdir -p ${SRS_OBJS}/research

        (cd ${SRS_WORKDIR}/research/hls && make ${SRS_JOBS} && mv ts_info ../../${SRS_OBJS_DIR}/research)
        ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/hls failed, ret=$ret"; exit $ret; fi

        (cd research/ffempty && make ${SRS_JOBS} && mv ffempty ../../${SRS_OBJS_DIR}/research)
        ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/ffempty failed, ret=$ret"; exit $ret; fi
    fi
fi

if [ $SRS_LIBRTMP = YES ]; then
    mkdir -p ${SRS_OBJS}/research
    
    # librtmp
    (cd ${SRS_WORKDIR}/research/librtmp && mkdir -p objs && ln -sf `pwd`/objs ../../${SRS_OBJS_DIR}/research/librtmp)
    ret=$?; if [[ $ret -ne 0 ]]; then echo "link research/librtmp failed, ret=$ret"; exit $ret; fi
fi

#####################################################################################
# build utest code
#####################################################################################
if [ $SRS_UTEST = YES ]; then
    if [[ -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]]; then
        echo "gtest-1.6.0 is ok.";
    else
        echo "build gtest-1.6.0"; 
        (
            rm -rf ${SRS_OBJS}/gtest-1.6.0 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/gtest-1.6.0.zip &&
            rm -rf gtest && ln -sf gtest-1.6.0 gtest
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build gtest-1.6.0 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/gtest/include/gtest/gtest.h ]; then echo "build gtest-1.6.0 failed."; exit -1; fi
fi

#####################################################################################
# build gperf code
#####################################################################################
if [ $SRS_GPERF = YES ]; then
    if [[ -f ${SRS_OBJS}/gperf/bin/pprof ]]; then
        echo "gperftools-2.1 is ok.";
    else
        echo "build gperftools-2.1"; 
        (
            rm -rf ${SRS_OBJS}/gperftools-2.1 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/gperftools-2.1.zip && cd gperftools-2.1 &&
            ./configure --prefix=`pwd`/_release --enable-frame-pointers && make ${SRS_JOBS} && make install &&
            cd .. && rm -rf gperf && ln -sf gperftools-2.1/_release gperf &&
            rm -rf pprof && ln -sf gperf/bin/pprof pprof
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build gperftools-2.1 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/gperf/bin/pprof ]; then echo "build gperftools-2.1 failed."; exit -1; fi
fi

#####################################################################################
# generated the test script
#####################################################################################
rm -rf ${SRS_OBJS}/srs.test && ln -sf `pwd`/scripts/srs.test objs/srs.test

