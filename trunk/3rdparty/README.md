http-parser-2.1.zip
* for srs to support http callback.
* https://github.com/nodejs/http-parser
* https://github.com/ossrs/http-parser
* https://ossrs.net/lts/zh-cn/license#http-parser

nginx-1.5.7.zip
* http://nginx.org/
* for srs to support hls streaming.

srt-1-fit
srt-1.4.1.tar.gz
* https://github.com/Haivision/srt/releases/tag/v1.4.1
* https://ossrs.net/lts/zh-cn/license#srt

openssl-1.1-fit
openssl-1.1.1l.tar.gz
* http://www.openssl.org/source/openssl-1.1.1l.tar.gz

openssl-1.1.0e.zip
openssl-OpenSSL_1_0_2u.tar.gz
* http://www.openssl.org/source/openssl-1.1.0e.tar.gz
* openssl for SRS(with-ssl) RTMP complex handshake to delivery h264+aac stream.
* SRTP depends on openssl 1.0.*, so we use both ssl versions.
* https://ossrs.net/lts/zh-cn/license#openssl

libsrtp-2.3.0.tar.gz
* For WebRTC, SRTP to encrypt and decrypt RTP.
* https://github.com/cisco/libsrtp/releases/tag/v2.3.0

ffmpeg-4.2.tar.gz
opus-1.3.1.tar.gz
* http://ffmpeg.org/releases/ffmpeg-4.2.tar.gz
* https://github.com/xiph/opus/releases/tag/v1.3.1
* To support RTMP/WebRTC transcoding.
* https://ossrs.net/lts/zh-cn/license#ffmpeg

gtest-fit
* google test framework.
* https://github.com/google/googletest/releases/tag/release-1.11.0

gperftools-2-fit
* gperf tools for performance benchmark.
* https://github.com/gperftools/gperftools/releases/tag/gperftools-2.9.1

st-srs
st-1.9.zip
state-threads
state-threads-1.9.1.tar.gz
* Patched ST from https://github.com/ossrs/state-threads
* https://ossrs.net/lts/zh-cn/license#state-threads

JSON
* https://github.com/udp/json-parser
* https://ossrs.net/lts/zh-cn/license#json

USRSCTP
* https://ossrs.net/lts/zh-cn/license#usrsctp

links:
* state-threads:
        https://github.com/ossrs/state-threads
* x264: 
        ftp://ftp.videolan.org/pub/videolan/x264/snapshots/x264-snapshot-20131129-2245-stable.tar.bz2
* lame: 
        http://nchc.dl.sourceforge.net/project/lame/lame/3.99/lame-3.99.5.tar.gz
* yasm:
        http://www.tortall.net/projects/yasm/releases/yasm-1.2.0.tar.gz
* speex:
        http://downloads.xiph.org/releases/speex/speex-1.2rc1.tar.gz
