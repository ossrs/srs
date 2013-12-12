#!/bin/bash

ff_src_dir="../../3rdparty"

ff_current_dir=$(pwd -P)
ff_build_dir="${ff_current_dir}/_build"
ff_release_dir="${ff_current_dir}/_release"
echo "start to build the tools for transcode system:"
echo "current_dir: ${ff_current_dir}"
echo "build_dir: ${ff_build_dir}"
echo "release_dir: ${ff_release_dir}"

mkdir -p ${ff_build_dir}
mkdir -p ${ff_release_dir}

# yasm for libx264
ff_yasm_bin=${ff_release_dir}/bin/yasm
if [[ -f ${ff_yasm_bin} ]]; then 
    echo "yasm is ok"
else
    echo "build yasm-1.2.0"
    cd $ff_current_dir &&
    rm -rf yasm-1.2.0 && unzip -q ${ff_src_dir}/yasm-1.2.0.zip &&
    cd yasm-1.2.0 && ./configure --prefix=${ff_release_dir} &&
    make && make install
    ret=$?; if [[ 0 -ne ${ret} ]]; then echo "build yasm-1.2.0 failed"; exit 1; fi
fi
# add yasm to path, for x264 to use yasm directly.
# ffmpeg can specifies the yasm path when configure it.
PATH=${PATH}:${ff_release_dir}/bin

# libaacplus
if [[ -f ${ff_release_dir}/lib/libaacplus.a ]]; then
    echo "libaacplus is ok"
else
    echo "build yasm-1.2.0"
    cd $ff_current_dir &&
    rm -rf libaacplus-2.0.2 && unzip -q ${ff_src_dir}/libaacplus-2.0.2.zip &&
    cd libaacplus-2.0.2 && cp ../${ff_src_dir}/libaacplus-patch-26410-800.zip src/26410-800.zip &&
    bash autogen.sh && ./configure --prefix=${ff_release_dir} --enable-static && make && make install
    ret=$?; if [[ 0 -ne ${ret} ]]; then echo "build libaacplus-2.0.2 failed"; exit 1; fi
fi

# lame-3.99
if [[ -f ${ff_release_dir}/lib/libmp3lame.a ]]; then
    echo "libmp3lame is ok"
else
    echo "build lame-3.99.5"
    cd $ff_current_dir &&
    rm -rf lame-3.99.5 && unzip -q ${ff_src_dir}/lame-3.99.5.zip &&
    cd lame-3.99.5 && ./configure --prefix=${ff_release_dir} --enable-static && make && make install
    ret=$?; if [[ 0 -ne ${ret} ]]; then echo "build lame-3.99.5 failed"; exit 1; fi
fi

# x264 core.138
if [[ -f ${ff_release_dir}/lib/libx264.a ]]; then
    echo "x264 is ok"
else
    echo "build x264"
    cd $ff_current_dir &&
    rm -rf x264-snapshot-20131129-2245-stable && unzip -q ${ff_src_dir}/x264-snapshot-20131129-2245-stable.zip &&
    cd x264-snapshot-20131129-2245-stable && ./configure --prefix=${ff_release_dir} --bit-depth=8 --enable-static && make && make install
    ret=$?; if [[ 0 -ne ${ret} ]]; then echo "build x264 failed"; exit 1; fi
fi

# ffmpeg-2.1.1
if [[ -f ${ff_release_dir}/bin/ffmpeg ]]; then
    echo "ffmpeg-2.1.1 is ok"
else
    echo "build ffmpeg-2.1.1"
    cd $ff_current_dir &&
    rm -rf ffmpeg-2.1.1 && unzip -q ${ff_src_dir}/ffmpeg-2.1.1.zip &&
    echo "remove all so to force the ffmpeg to build in static" &&
    rm -f ${ff_release_dir}/lib/*.so* &&
    echo "export the dir to enable the build command canbe use." &&
    export ffmpeg_exported_release_dir=${ff_release_dir} &&
    cd ffmpeg-2.1.1 && 
    ./configure \
        --enable-gpl --enable-nonfree \
        --yasmexe=${ff_yasm_bin} \
        --prefix=${ff_release_dir} --cc= \
        --enable-static --disable-shared --disable-debug \
        --extra-cflags='-I${ffmpeg_exported_release_dir}/include -I /usr/include/freetype2/' \
        --extra-ldflags='-L${ffmpeg_exported_release_dir}/lib -lm -ldl' \
        --disable-ffplay --disable-ffprobe --disable-ffserver --disable-doc \
        --enable-postproc --enable-bzlib --enable-zlib --enable-parsers \
        --enable-libfreetype \
        --enable-libx264 --enable-libmp3lame --enable-libaacplus \
        --enable-pthreads --extra-libs=-lpthread --enable-encoders --enable-decoders --enable-avfilter --enable-muxers --enable-demuxers && 
    make && make install
    ret=$?; if [[ 0 -ne ${ret} ]]; then echo "build x264 failed"; exit 1; fi
fi
