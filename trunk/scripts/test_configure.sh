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

log="${build_objs}/logs/package.`date +%s`.log" && . ${product_dir}/scripts/_log.sh && check_log
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi

item="default configure"
ok_msg "test ${item}"
(./configure && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"

item="preset --x86-x64"
ok_msg "test ${item}"
(./configure --x86-x64 && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"

item="preset --dev"
ok_msg "test ${item}"
(./configure --dev && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"

item="preset --fast"
ok_msg "test ${item}"
(./configure --fast && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"

item="preset --pure-rtmp"
ok_msg "test ${item}"
(./configure --pure-rtmp && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"

item="preset --rtmp-hls"
ok_msg "test ${item}"
(./configure --rtmp-hls && make) >>$log 2>&1
ret=$?; if [[ $ret -ne 0 ]]; then failed_msg "test ${item} failed. ret=$ret"; exit $ret; fi
ok_msg "test ${item} success"
