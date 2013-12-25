#!/bin/bash

# TODO: check gcc/g++
echo "check gcc/g++/gdb/make/openssl-devel"
echo "depends tools are ok"
#####################################################################################
# for Ubuntu
#####################################################################################
function Ubuntu_prepare()
{
    uname -v|grep Ubuntu >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        return;
    fi

    echo "Ubuntu detected, install tools if needed"
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc"
        require_sudoer "sudo apt-get install -y gcc"
        sudo apt-get install -y gcc
        echo "install gcc success"
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install g++"
        require_sudoer "sudo apt-get install -y g++"
        sudo apt-get install -y g++
        echo "install g++ success"
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install make"
        require_sudoer "sudo apt-get install -y make"
        sudo apt-get install -y make
        echo "install make success"
    fi
    
    autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install autoconf"
        require_sudoer "sudo apt-get install -y autoconf"
        sudo apt-get install -y autoconf
        echo "install autoconf success"
    fi
    
    libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install libtool"
        require_sudoer "sudo apt-get install -y libtool"
        sudo apt-get install -y libtool
        echo "install libtool success"
    fi
    
    if [[ ! -f /usr/include/pcre.h ]]; then
        echo "install libpcre3-dev"
        require_sudoer "sudo apt-get install -y libpcre3-dev"
        sudo apt-get install -y libpcre3-dev
        echo "install libpcre3-dev success"
    fi
    
    if [[ ! -f /usr/include/zlib.h ]]; then
        echo "install zlib1g-dev"
        require_sudoer "sudo apt-get install -y zlib1g-dev"
        sudo apt-get install -y zlib1g-dev
        echo "install zlib1g-dev success"
    fi
    
    if [[ ! -d /usr/include/freetype2 ]]; then
        echo "install libfreetype6-dev"
        require_sudoer "sudo apt-get install -y libfreetype6-dev"
        sudo apt-get install -y libfreetype6-dev
        echo "install libfreetype6-dev success"
    fi
    
    if [[ ! -d /usr/include/openssl ]]; then
        echo "install libssl-dev"
        require_sudoer "sudo apt-get install -y libssl-dev"
        sudo apt-get install -y libssl-dev
        echo "install libssl-dev success"
    fi
    
    echo "Ubuntu install tools success"
}
Ubuntu_prepare
#####################################################################################
# for Centos
#####################################################################################
function Centos_prepare()
{
    if [[ ! -f /etc/redhat-release ]]; then
        return;
    fi

    echo "Centos detected, install tools if needed"
    
    gcc --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc"
        require_sudoer "sudo yum install -y gcc"
        sudo yum install -y gcc
        echo "install gcc success"
    fi
    
    g++ --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install gcc-c++"
        require_sudoer "sudo yum install -y gcc-c++"
        sudo yum install -y gcc-c++
        echo "install gcc-c++ success"
    fi
    
    make --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install make"
        require_sudoer "sudo yum install -y make"
        sudo yum install -y make
        echo "install make success"
    fi
    
    automake --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install automake"
        require_sudoer "sudo yum install -y automake"
        sudo yum install -y automake
        echo "install automake success"
    fi
    
    autoconf --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install autoconf"
        require_sudoer "sudo yum install -y autoconf"
        sudo yum install -y autoconf
        echo "install autoconf success"
    fi
    
    libtool --help >/dev/null 2>&1; ret=$?; if [[ 0 -ne $ret ]]; then
        echo "install libtool"
        require_sudoer "sudo yum install -y libtool"
        sudo yum install -y libtool
        echo "install libtool success"
    fi
    
    if [[ ! -f /usr/include/pcre.h ]]; then
        echo "install pcre-devel"
        require_sudoer "sudo yum install -y pcre-devel"
        sudo yum install -y pcre-devel
        echo "install pcre-devel success"
    fi
    
    if [[ ! -f /usr/include/zlib.h ]]; then
        echo "install zlib-devel"
        require_sudoer "sudo yum install -y zlib-devel"
        sudo yum install -y zlib-devel
        echo "install zlib-devel success"
    fi
    
    if [[ ! -d /usr/include/freetype2 ]]; then
        echo "install freetype-devel"
        require_sudoer "sudo yum install -y freetype-devel"
        sudo yum install -y freetype-devel
        echo "install freetype-devel success"
    fi
    
    if [[ ! -d /usr/include/openssl ]]; then
        echo "install openssl-devel"
        require_sudoer "sudo yum install -y openssl-devel"
        sudo yum install -y openssl-devel
        echo "install openssl-devel success"
    fi
    
    echo "Centos install tools success"
}
Centos_prepare

#####################################################################################
# st-1.9
#####################################################################################
if [[ -f ${SRS_OBJS}/st-1.9/obj/libst.a && -f ${SRS_OBJS}/st-1.9/obj/libst.so ]]; then
    echo "st-1.9t is ok.";
else
    echo "build st-1.9t"; 
    (
        rm -rf ${SRS_OBJS}/st-1.9 && cd ${SRS_OBJS} && 
        unzip -q ../3rdparty/st-1.9.zip && cd st-1.9 && make linux-debug &&
        cd .. && rm -f st && ln -sf st-1.9/obj st
    )
fi
# check status
ret=$?; if [[ $ret -ne 0 ]]; then echo "build st-1.9 failed, ret=$ret"; exit $ret; fi
if [ ! -f ${SRS_OBJS}/st-1.9/obj/libst.a ]; then echo "build st-1.9 failed."; exit -1; fi
if [ ! -f ${SRS_OBJS}/st-1.9/obj/libst.so ]; then echo "build st-1.9 failed."; exit -1; fi

#####################################################################################
# http-parser-2.1
#####################################################################################
if [ $SRS_HTTP = YES ]; then
    if [[ -f ${SRS_OBJS}/http-parser-2.1/http_parser.h && -f ${SRS_OBJS}/http-parser-2.1/libhttp_parser.a ]]; then
        echo "http-parser-2.1 is ok.";
    else
        echo "build http-parser-2.1";
        (
            rm -rf ${SRS_OBJS}/http-parser-2.1 && cd ${SRS_OBJS} && unzip -q ../3rdparty/http-parser-2.1.zip && 
            cd http-parser-2.1 && 
            sed -i "s/CPPFLAGS_FAST +=.*$/CPPFLAGS_FAST = \$\(CPPFLAGS_DEBUG\)/g" Makefile &&
            sed -i "s/CFLAGS_FAST =.*$/CFLAGS_FAST = \$\(CFLAGS_DEBUG\)/g" Makefile &&
            make package &&
            cd .. && rm -f hp && ln -sf http-parser-2.1 hp
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build http-parser-2.1 failed, ret=$ret"; exit $ret; fi
    if [[ ! -f ${SRS_OBJS}/http-parser-2.1/http_parser.h ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
    if [[ ! -f ${SRS_OBJS}/http-parser-2.1/libhttp_parser.a ]]; then echo "build http-parser-2.1 failed"; exit -1; fi
fi

#####################################################################################
# nginx for HLS, nginx-1.5.0
#####################################################################################
function write_nginx_html5()
{
    cat<<END >> ${html_file}
<video width="640" height="360"
        autoplay controls autobuffer 
        src="${hls_stream}"
        type="application/vnd.apple.mpegurl">
</video>
END
}
if [ $SRS_HLS = YES ]; then
    if [[ -f ${SRS_OBJS}/nginx/sbin/nginx ]]; then
        echo "nginx-1.5.7 is ok.";
    else
        echo "build nginx-1.5.7"; 
        (
            rm -rf ${SRS_OBJS}/nginx-1.5.7 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/nginx-1.5.7.zip && cd nginx-1.5.7 && 
            ./configure --prefix=`pwd`/_release && make && make install &&
            cd .. && ln -sf nginx-1.5.7/_release nginx
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
    
    # override the default index.
    cat <<END > ${SRS_OBJS}/nginx/html/index.html
    <script type="text/javascript">
        window.location.href = "players/index.html";
    </script>
END
fi

if [ $SRS_HLS = YES ]; then
    echo "#define SRS_HLS" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HLS" >> $SRS_AUTO_HEADERS_H
fi

#####################################################################################
# cherrypy for http hooks callback, CherryPy-3.2.4
#####################################################################################
if [ $SRS_HTTP = YES ]; then
    if [[ -f ${SRS_OBJS}/CherryPy-3.2.4/setup.py ]]; then
        echo "CherryPy-3.2.4 is ok.";
    else
        require_sudoer "configure --with-http"
        echo "install CherryPy-3.2.4"; 
        (
            sudo rm -rf ${SRS_OBJS}/CherryPy-3.2.4 && cd ${SRS_OBJS} && 
            unzip -q ../3rdparty/CherryPy-3.2.4.zip && cd CherryPy-3.2.4 && 
            sudo python setup.py install
        )
    fi
    # check status
    ret=$?; if [[ $ret -ne 0 ]]; then echo "build CherryPy-3.2.4 failed, ret=$ret"; exit $ret; fi
    if [ ! -f ${SRS_OBJS}/nginx/sbin/nginx ]; then echo "build CherryPy-3.2.4 failed."; exit -1; fi
fi

if [ $SRS_HTTP = YES ]; then
    echo "#define SRS_HTTP" >> $SRS_AUTO_HEADERS_H
else
    echo "#undef SRS_HTTP" >> $SRS_AUTO_HEADERS_H
fi

echo "link players to cherrypy static-dir"
rm -f research/api-server/static-dir/players &&
ln -sf `pwd`/research/players research/api-server/static-dir/players

#####################################################################################
# openssl, for rtmp complex handshake
#####################################################################################
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
            cd ${pwd_dir} && ln -sf ffmpeg.src/_release ffmpeg
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
mkdir -p ${SRS_OBJS}/research

(cd research/hls && make && mv ts_info ../../${SRS_OBJS}/research)
ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/hls failed, ret=$ret"; exit $ret; fi

(cd research/ffempty && make && mv ffempty ../../${SRS_OBJS}/research)
ret=$?; if [[ $ret -ne 0 ]]; then echo "build research/ffempty failed, ret=$ret"; exit $ret; fi
