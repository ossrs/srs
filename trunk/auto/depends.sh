#!/bin/bash

# variables, parent script must set it:
# SRS_JOBS: the build jobs.
# SrsArmMakeOptions: the arm make options for ubuntu12(armhf, v7cpu)
# SRS_AUTO_HEADERS_H: the auto generated header file.

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
function Ubuntu_prepare()
{
    uname -v|grep Ubuntu >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        return 0;
    fi

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
    
    if [[ ! -f /usr/include/pcre.h ]]; then
        echo "install libpcre3-dev"
        require_sudoer "sudo apt-get install -y --force-yes libpcre3-dev"
        sudo apt-get install -y --force-yes libpcre3-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install libpcre3-dev success"
    fi
    
    if [[ ! -f /usr/include/zlib.h ]]; then
        echo "install zlib1g-dev"
        require_sudoer "sudo apt-get install -y --force-yes zlib1g-dev"
        sudo apt-get install -y --force-yes zlib1g-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install zlib1g-dev success"
    fi
    
    if [[ ! -d /usr/include/freetype2 ]]; then
        echo "install libfreetype6-dev"
        require_sudoer "sudo apt-get install -y --force-yes libfreetype6-dev"
        sudo apt-get install -y --force-yes libfreetype6-dev; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install libfreetype6-dev success"
    fi
    
    # for arm, install the cross build tool chain.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        arm-linux-gnueabi-gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
            echo "install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi"
            require_sudoer "sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi"
            sudo apt-get install -y --force-yes gcc-arm-linux-gnueabi g++-arm-linux-gnueabi; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
            echo "install gcc-arm-linux-gnueabi g++-arm-linux-gnueabi success"
        fi
    fi
    
    echo "Ubuntu install tools success"
    return 0
}
Ubuntu_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "Ubuntu prepare failed, ret=$ret"; exit $ret; fi
#####################################################################################
# for Centos, auto install tools by yum
#####################################################################################
function Centos_prepare()
{
    if [[ ! -f /etc/redhat-release ]]; then
        return 0;
    fi

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
    
    if [[ ! -f /usr/include/pcre.h ]]; then
        echo "install pcre-devel"
        require_sudoer "sudo yum install -y pcre-devel"
        sudo yum install -y pcre-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install pcre-devel success"
    fi
    
    if [[ ! -f /usr/include/zlib.h ]]; then
        echo "install zlib-devel"
        require_sudoer "sudo yum install -y zlib-devel"
        sudo yum install -y zlib-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install zlib-devel success"
    fi
    
    if [[ ! -d /usr/include/freetype2 ]]; then
        echo "install freetype-devel"
        require_sudoer "sudo yum install -y freetype-devel"
        sudo yum install -y freetype-devel; ret=$?; if [[ 0 -ne $ret ]]; then return $ret; fi
        echo "install freetype-devel success"
    fi
    
    # for arm, install the cross build tool chain.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        echo "arm-ubuntu12 is invalid for CentOS"
        return 1
    fi
    
    echo "Centos install tools success"
    return 0
}
Centos_prepare; ret=$?; if [[ 0 -ne $ret ]]; then echo "CentOS prepare failed, ret=$ret"; exit $ret; fi

#####################################################################################
# st-1.9
#####################################################################################
# check the arm flag file, if flag changed, need to rebuild the st.
if [ $SRS_ARM_UBUNTU12 = YES ]; then
    # ok, arm specified, if the flag filed does not exists, need to rebuild.
    if [[ -f ${SRS_OBJS}/_flag.st.arm.tmp && -f ${SRS_OBJS}/st/libst.a && -f ${SRS_OBJS}/st/libst.so ]]; then
        echo "st-1.9t for arm is ok.";
    else
        # TODO: FIXME: patch the bug.
        # patch st for arm, @see: https://github.com/winlinvip/simple-rtmp-server/wiki/SrsLinuxArm#st-arm-bug-fix
        echo "build st-1.9t for arm"; 
        (
            rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && 
            patch -p0 < ../../3rdparty/patches/1.st.arm.patch &&
            make CC=${SrsArmCC} AR=${SrsArmAR} LD=${SrsArmLD} RANDLIB=${SrsArmRANDLIB} EXTRA_CFLAGS="-DMD_HAVE_EPOLL" linux-debug &&
            cd .. && rm -rf st && ln -sf st-1.9/obj st &&
            cd .. && touch ${SRS_OBJS}/_flag.st.arm.tmp
        )
    fi
else
    # arm not specified, if exists flag, need to rebuild for no-arm platform.
    if [[ ! -f ${SRS_OBJS}/_flag.st.arm.tmp && -f ${SRS_OBJS}/st/libst.a && -f ${SRS_OBJS}/st/libst.so ]]; then
        echo "st-1.9t is ok.";
    else
        echo "build st-1.9t"; 
        (
            rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && 
            echo "we alaways patch the st, for we may build srs under arm directly" &&
            echo "the 1.st.arm.patch is ok for x86 because it's only modify code under macro linux arm" &&
            patch -p0 < ../../3rdparty/patches/1.st.arm.patch &&
            make EXTRA_CFLAGS="-DMD_HAVE_EPOLL" linux-debug &&
            cd .. && rm -rf st && ln -sf st-1.9/obj st &&
            cd .. && rm -f ${SRS_OBJS}/_flag.st.arm.tmp
        )
    fi
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "build st-1.9 failed, ret=$ret"; exit $ret; fi
if [ ! -f ${SRS_OBJS}/st/libst.a ]; then echo "build st-1.9 failed."; exit -1; fi
if [ ! -f ${SRS_OBJS}/st/libst.so ]; then echo "build st-1.9 failed."; exit -1; fi

#####################################################################################
# http-parser-2.1
#####################################################################################
if [ $SRS_HTTP_PARSER = YES ]; then
    if [[ -f ${SRS_OBJS}/hp/http_parser.h && -f ${SRS_OBJS}/hp/libhttp_parser.a ]]; then
        echo "http-parser-2.1 is ok.";
    else
        echo "build http-parser-2.1";
        (
            rm -rf ${SRS_OBJS}/http-parser-2.1 && cd ${SRS_OBJS} && unzip -q ../3rdparty/http-parser-2.1.zip && 
            cd http-parser-2.1 && 
            sed -i "s/CPPFLAGS_FAST +=.*$/CPPFLAGS_FAST = \$\(CPPFLAGS_DEBUG\)/g" Makefile &&
            sed -i "s/CFLAGS_FAST =.*$/CFLAGS_FAST = \$\(CFLAGS_DEBUG\)/g" Makefile &&
            make package &&
            cd .. && rm -rf hp && ln -sf http-parser-2.1 hp
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build http-parser-2.1 failed, ret=$ret"; exit $ret; fi
    if [[ ! -f ${SRS_OBJS}/hp/http_parser.h ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
    if [[ ! -f ${SRS_OBJS}/hp/libhttp_parser.a ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
fi

if [ $SRS_HTTP_PARSER = YES ]; then
    echo "#define SRS_HTTP_PARSER" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HTTP_PARSER" >> $SRS_AUTO_HEADERS_H
fi

if [ $SRS_HTTP_SERVER = YES ]; then
    echo "#define SRS_HTTP_SERVER" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HTTP_SERVER" >> $SRS_AUTO_HEADERS_H
fi

if [ $SRS_HTTP_API = YES ]; then
    echo "#define SRS_HTTP_API" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HTTP_API" >> $SRS_AUTO_HEADERS_H
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
__SRS_BUILD_NGINX=NO; if [ $SRS_ARM_UBUNTU12 = NO ]; then if [ $SRS_NGINX = YES ]; then __SRS_BUILD_NGINX=YES; fi fi
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
    sed -i "s/^.user  nobody;/user `whoami`;/g" ${SRS_OBJS}/nginx/conf/nginx.conf
    
    # create forward dir
    mkdir -p ${SRS_OBJS}/nginx/html/live &&
    mkdir -p ${SRS_OBJS}/nginx/html/forward/live
    
    # generate default html pages for android.
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
    
    # nginx.html to detect whether nginx is alive
    echo "nginx is ok" > ${SRS_OBJS}/nginx/html/nginx.html
fi

if [ $SRS_NGINX = YES ]; then
    echo "#define SRS_NGINX" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_NGINX" >> $SRS_AUTO_HEADERS_H
fi

if [ $SRS_HLS = YES ]; then
    echo "#define SRS_HLS" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HLS" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# cherrypy for http hooks callback, CherryPy-3.2.4
#####################################################################################
if [ $SRS_HTTP_CALLBACK = YES ]; then
    if [[ -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        require_sudoer "configure --with-http-callback"
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

if [ $SRS_HTTP_CALLBACK = YES ]; then
    echo "#define SRS_HTTP_CALLBACK" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HTTP_CALLBACK" >> $SRS_AUTO_HEADERS_H
fi

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

# only when the nginx is ok, 
# if api-server not enalbed, use nginx as demo.
if [ $__SRS_BUILD_NGINX = YES ]; then
    if [ $SRS_HTTP_CALLBACK = YES ]; then
        # override the default index.
        rm -f ${SRS_OBJS}/nginx/html/index.html &&
        ln -sf `pwd`/research/players/nginx_index.html ${SRS_OBJS}/nginx/html/index.html
    else
        rm -f ${SRS_OBJS}/nginx/html/index.html &&
        cat<<END > ${SRS_OBJS}/nginx/html/index.html
<!DOCTYPE html>
<html>
<head>
    <title>SRS</title>   
    <meta charset="utf-8">
</head>
<body>
    <script type="text/javascript">
        setTimeout(function(){
            window.location.href = "players/index.html" + window.location.search;
        }, 500);
    </script>
</body>
END
    fi
fi

#####################################################################################
# openssl, for rtmp complex handshake
#####################################################################################
if [ $SRS_SSL = YES ]; then
    # check the arm flag file, if flag changed, need to rebuild the st.
    if [ $SRS_ARM_UBUNTU12 = YES ]; then
        # ok, arm specified, if the flag filed does not exists, need to rebuild.
        if [[ -f ${SRS_OBJS}/_flag.ssl.arm.tmp && -f ${SRS_OBJS}/openssl/lib/libssl.a ]]; then
            echo "openssl-1.0.1f for arm is ok.";
        else
            echo "build openssl-1.0.1f for arm"; 
            (
                rm -rf ${SRS_OBJS}/openssl-1.0.1f && cd ${SRS_OBJS} && 
                unzip -q ../3rdparty/openssl-1.0.1f.zip && cd openssl-1.0.1f && 
                ./Configure --prefix=`pwd`/_release -no-shared no-asm linux-armv4 && 
                make CC=${SrsArmCC} GCC=${SrsArmGCC} AR="${SrsArmAR} r" LD=${SrsArmLD} LINK=${SrsArmGCC} RANDLIB=${SrsArmRANDLIB} && make install &&
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
                ./config --prefix=`pwd`/_release -no-shared && 
                make && make install &&
                cd .. && rm -rf openssl && ln -sf openssl-1.0.1f/_release openssl &&
                cd .. && rm -f ${SRS_OBJS}/_flag.ssl.arm.tmp
            )
        fi
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build openssl-1.0.1f failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/openssl/lib/libssl.a ]; then echo "build openssl-1.0.1f failed."; exit -1; fi
fi

if [ $SRS_SSL = YES ]; then
    echo "#define SRS_SSL" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_SSL" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# live transcoding, ffmpeg-2.1, x264-core138, lame-3.99.5, libaacplus-2.0.2.
#####################################################################################
if [ $SRS_FFMPEG = YES ]; then
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

if [ $SRS_FFMPEG = YES ]; then
    echo "#define SRS_FFMPEG" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_FFMPEG" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# build research code
#####################################################################################
if [ $SRS_RESEARCH = YES ]; then
    mkdir -p ${SRS_OBJS}/research

    (cd research/hls && make ${SRS_JOBS} && mv ts_info ../../${SRS_OBJS}/research)
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/hls failed, ret=$ret"; exit $ret; fi

    (cd research/ffempty && make ${SRS_JOBS} && mv ffempty ../../${SRS_OBJS}/research)
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/ffempty failed, ret=$ret"; exit $ret; fi
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

if [ $SRS_GPERF = YES ]; then
    echo "#define SRS_GPERF" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_GPERF" >> $SRS_AUTO_HEADERS_H
fi
if [ $SRS_GPERF_MC = YES ]; then
    echo "#define SRS_GPERF_MC" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_GPERF_MC" >> $SRS_AUTO_HEADERS_H
fi
if [ $SRS_GPERF_MP = YES ]; then
    echo "#define SRS_GPERF_MP" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_GPERF_MP" >> $SRS_AUTO_HEADERS_H
fi
if [ $SRS_GPERF_CP = YES ]; then
    echo "#define SRS_GPERF_CP" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_GPERF_CP" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# for arm.
#####################################################################################
if [ $SRS_ARM_UBUNTU12 = YES ]; then
    echo "#define SRS_ARM_UBUNTU12" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_ARM_UBUNTU12" >> $SRS_AUTO_HEADERS_H
fi

echo "" >> $SRS_AUTO_HEADERS_H
# prefix
echo "#define SRS_PREFIX \"${SRS_PREFIX}\"" >> $SRS_AUTO_HEADERS_H

echo "" >> $SRS_AUTO_HEADERS_H

#####################################################################################
# generated the contributors from AUTHORS.txt
#####################################################################################
SRS_CONSTRIBUTORS=`cat ../AUTHORS.txt|grep "*"|awk -F '* ' '{print $2}'`
echo "#define SRS_CONSTRIBUTORS \"\\" >> $SRS_AUTO_HEADERS_H
for CONTRIBUTOR in $SRS_CONSTRIBUTORS; do
    echo "${CONTRIBUTOR} \\" >> $SRS_AUTO_HEADERS_H
done
echo "\"" >> $SRS_AUTO_HEADERS_H

# new empty line to auto headers file.
echo "" >> $SRS_AUTO_HEADERS_H
