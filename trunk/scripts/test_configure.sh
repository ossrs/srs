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

# test default configure.
