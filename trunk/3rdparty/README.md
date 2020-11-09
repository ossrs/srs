http-parser-2.1.zip
* for srs to support http callback.
* https://github.com/joyent/http-parser
* https://github.com/ossrs/srs/wiki/LicenseMixing#http-parser

nginx-1.5.7.zip
* http://nginx.org/
* for srs to support hls streaming.

srt-1-fit
srt-1.4.1.tar.gz
* https://github.com/Haivision/srt/releases/tag/v1.4.1
* https://github.com/ossrs/srs/wiki/LicenseMixing#srt

openssl-1.1-fit
openssl-1.1.1b.tar.gz
* http://www.openssl.org/source/openssl-1.1.1b.tar.gz

openssl-1.1.0e.zip
openssl-OpenSSL_1_0_2u.tar.gz
* http://www.openssl.org/source/openssl-1.1.0e.tar.gz
* openssl for SRS(with-ssl) RTMP complex handshake to delivery h264+aac stream.
* SRTP depends on openssl 1.0.*, so we use both ssl versions.
* https://github.com/ossrs/srs/wiki/LicenseMixing#openssl

CherryPy-3.2.4.zip
* sample api server for srs.
* https://pypi.python.org/pypi/CherryPy/3.2.4

libsrtp-2.3.0.tar.gz
* For WebRTC, SRTP to encrypt and decrypt RTP.
* https://github.com/cisco/libsrtp/releases/tag/v2.3.0

ffmpeg-4.2.tar.gz
opus-1.3.1.tar.gz
* http://ffmpeg.org/releases/ffmpeg-4.2.tar.gz
* https://github.com/xiph/opus/releases/tag/v1.3.1
* To support RTMP/WebRTC transcoding.
* https://github.com/ossrs/srs/wiki/LicenseMixing#ffmpeg
    
gtest-1.6.0.zip
* google test framework.
* https://code.google.com/p/googletest/downloads/list
    
gperftools-2.1.zip
* gperf tools for performance benchmark.
* https://code.google.com/p/gperftools/downloads/list

st-srs
st-1.9.zip
state-threads
state-threads-1.9.1.tar.gz
* Patched ST from https://github.com/ossrs/state-threads
* https://github.com/ossrs/srs/wiki/LicenseMixing#state-threads

JSON
* https://github.com/udp/json-parser
* https://github.com/ossrs/srs/wiki/LicenseMixing#json

USRSCTP
* https://github.com/ossrs/srs/wiki/LicenseMixing#usrsctp

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
* srtp:
        https://github.com/cisco/libsrtp/releases/tag/v2.3.0
* usrsctp: 
        https://github.com/sctplab/usrsctp
