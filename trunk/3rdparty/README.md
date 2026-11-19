http-parser-2.1.zip
* for srs to support http callback.
* https://github.com/nodejs/http-parser
* https://github.com/ossrs/http-parser
* https://ossrs.net/lts/zh-cn/license#http-parser

nginx-1.5.7.zip
* http://nginx.org/
* for srs to support hls streaming.

srt-1-fit
srt-1.5.6.tar.gz
* https://github.com/Haivision/srt/releases/tag/v1.5.6
* https://ossrs.net/lts/zh-cn/license#srt

openssl-1.1-fit
openssl-1.1.1l.tar.gz
* http://www.openssl.org/source/openssl-1.1.1l.tar.gz

openssl-4-fit
openssl-4.1.0.tar.gz
* https://github.com/openssl/openssl/releases/download/openssl-4.1.0/openssl-4.1.0.tar.gz
* sha256 50c17baf54cb008f6ce42d50deb4b28050a1aad711636cabd4b241c7e90cc2fd
* Built by --ssl-4=on, for DTLS 1.3, with the features SRS does not use turned off, see the --ssl-4=on options
  in trunk/auto/depends.sh. Assembly is on, as for openssl-1.1-fit, and off (-no-asm) on Loongson and with
  --nasm=off, so the tree builds both ways.
* Trimmed to the files those builds read, with assembly and with -no-asm, plus the few in
  openssl-4-trim/keep-extra.txt: 1498 files and 649,556 lines, of which 102 files and 157,780 lines are
  assembly and perlasm, of the 2506 files and 1,051,667 lines left after removing test, doc (except
  build.info), demos and release notes. The builds are macOS arm64 and x86_64, and Linux x86_64, aarch64,
  armv4, mips32, generic32, mips64, riscv64 and loongarch64. The native Windows build uses vcpkg's OpenSSL,
  not this tree.
* Patched with the ssl/ part of https://github.com/openssl/openssl/commit/59ecb831dbc6a87f1a71bd8fd278c6e1b242dd42
  from the openssl-4.1 branch, for 4.1.1: a DTLS 1.3 server took a client's ACK of part of its first flight as
  an unexpected message and aborted the handshake, https://github.com/openssl/openssl/issues/33242
* A build cache from an older tree or other options keeps the old library: remove the openssl, srtp2 and srt
  directories in objs/Platform-*-SSL4/3rdparty to rebuild them.
* To upgrade to a new 4.x, on a Mac with Docker, from trunk:
  1. Replace openssl-4-fit with the new release, without test, doc (except build.info), demos and release
     notes, and apply the patch above if the release does not have it.
  2. Run `bash 3rdparty/openssl-4-trim/run.sh <work-dir>`, with <work-dir> outside the repository. It builds the
     tree with the depends.sh options for every target above, once with assembly and once with -no-asm, and
     writes the files no build reads to <work-dir>/report/drop.txt. Every target must build both ways and pass
     its cross-check; <work-dir>/report/status.txt has a line for each.
  3. Read drop.txt, then delete exactly those files: `sed 's|^|3rdparty/openssl-4-fit/|' <work-dir>/report/drop.txt`
     into a list, and `xargs git rm -q -- < <list>`.
  4. Run run.sh again with a new <work-dir>, then `bash 3rdparty/openssl-4-trim/check.sh 3rdparty/openssl-4-fit
     <work-dir>`, which fails if a target does not build with assembly or with -no-asm, or on any file no build
     reads. Configure checks that the sources of libraries it does not build, such as libfips and liblegacy,
     exist without reading them: if it reports a file "is given as source ... but is missing", restore that file with
     `git restore --staged --worktree --source=HEAD -- <file>`, add it to keep-extra.txt and repeat this step.
  5. Build SRS with `--ssl-4=on` after removing the cache above, and run the `OpenSsl*` and `Dtls13*` utests.

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
