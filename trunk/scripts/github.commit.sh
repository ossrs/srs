#!/bin/bash

echo "submit code to github.com"

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

# allow start script from any dir
cd $work_dir && git checkout master

. ${product_dir}/scripts/_log.sh
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi
ok_msg "导入脚本成功"

source $work_dir/scripts/_mirror.utils.sh

git remote -v|grep github.com >/dev/null 2>&1
ret=$?; if [[ 0 -ne $ret ]]; then 
    echo "current not under github.com branch"
    exit -1; 
fi 

sync_push "Github"

exit 0
