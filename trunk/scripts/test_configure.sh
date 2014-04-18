#!/bin/bash

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
(./configure && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --x86-x64"
ok_msg "test \" ${item} \""
(./configure --x86-x64 && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --disable-all"
ok_msg "test \" ${item} \""
(./configure --disable-all && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast"
ok_msg "test \" ${item} \""
(./configure --fast && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --pure-rtmp"
ok_msg "test \" ${item} \""
(./configure --pure-rtmp && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --rtmp-hls"
ok_msg "test \" ${item} \""
(./configure --rtmp-hls && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ssl"
ok_msg "test \" ${item} \""
(./configure --fast --with-ssl && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-hls"
ok_msg "test \" ${item} \""
(./configure --fast --with-hls && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-dvr"
ok_msg "test \" ${item} \""
(./configure --fast --with-dvr && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-nginx"
ok_msg "test \" ${item} \""
(./configure --fast --with-nginx && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-callback"
ok_msg "test \" ${item} \""
(./configure --fast --with-http-callback && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-server"
ok_msg "test \" ${item} \""
(./configure --fast --with-http-server && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-http-api"
ok_msg "test \" ${item} \""
(./configure --fast --with-http-api && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ffmpeg"
ok_msg "test \" ${item} \""
(./configure --fast --with-ffmpeg && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-transcode"
ok_msg "test \" ${item} \""
(./configure --fast --with-transcode && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ingest"
ok_msg "test \" ${item} \""
(./configure --fast --with-ingest && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-librtmp"
ok_msg "test \" ${item} \""
(./configure --fast --with-librtmp && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-bwtc"
ok_msg "test \" ${item} \""
(./configure --fast --with-bwtc && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-research"
ok_msg "test \" ${item} \""
(./configure --fast --with-research && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-utest"
ok_msg "test \" ${item} \""
(./configure --fast --with-utest && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-ssl --with-utest"
ok_msg "test \" ${item} \""
(./configure --fast --with-ssl --with-utest && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf"
ok_msg "test \" ${item} \""
(./configure --fast --with-gperf && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gmc"
ok_msg "test \" ${item} \""
(./configure --fast --with-gperf --with-gmc && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gmp"
ok_msg "test \" ${item} \""
(./configure --fast --with-gperf --with-gmp && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gperf --with-gcp"
ok_msg "test \" ${item} \""
(./configure --fast --with-gperf --with-gcp && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

item="./configure --fast --with-gprof"
ok_msg "test \" ${item} \""
(./configure --fast --with-gprof && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

#######################################################################################################
#######################################################################################################
#######################################################################################################
item="./configure --dev"
ok_msg "test \" ${item} \""
(./configure --dev && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test \" ${item} \" failed. ret=$ret"; exit $ret; fi
ok_msg "test \" ${item} \" success"

echo "success"
