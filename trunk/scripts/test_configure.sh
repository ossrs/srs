#!/bin/bash

params="$@"
echo "params is: $params"

# discover the current work dir, the log and access.
echo "argv[0]=$0"
if [[ ! -f $0 ]]; then 
    echo "directly execute the scripts on shell.";
    work_dir=`pwd`
else 
    echo "execute scripts in file: $0";
    work_dir=`dirname $0`; work_dir=`(cd ${work_dir} && pwd)`
fi
work_dir=`(cd ${work_dir}/.. && pwd)`
product_dir=$work_dir
build_objs=${work_dir}/objs
package_dir=${build_objs}/package

log="${build_objs}/test.`date +%s`.log" && . ${product_dir}/scripts/_log.sh && check_log
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi

item="./configure"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --x86-x64"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --pure-rtmp"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ssl"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-hls"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-dvr"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-nginx"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-callback"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-server"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-api"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ffmpeg"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-transcode"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ingest"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-librtmp"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-research"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-stream-caster --with-http-api"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-utest"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ssl --with-utest"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gmc"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gmp"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gcp"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gprof"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --log-verbose"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --log-info"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --log-trace"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --log-info --log-verbose --log-trace"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

#######################################################################################################
#######################################################################################################
#######################################################################################################
item="./configure --dev"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast-dev"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --demo"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --full"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

#######################################################################################################
#######################################################################################################
#######################################################################################################
item="./configure --disable-all --with-ssl"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-hls --with-ssl --with-http-server --with-http-api"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-ssl --with-hls --with-nginx"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-ssl --with-hls --with-nginx --with-ffmpeg --with-transcode"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-ssl --with-ffmpeg --with-transcode"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-ssl --with-ffmpeg --with-ingest"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all --with-hls --with-ssl --with-http-server"
ok_msg "test \" ${item} \""
($item && make $params) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

echo "success"
