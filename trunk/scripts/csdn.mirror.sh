#!/bin/bash

echo "更新CSDN镜像的脚本"

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
cd $work_dir

. ${product_dir}/scripts/_log.sh
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi
ok_msg "导入脚本成功"

source $work_dir/scripts/_mirror.utils.sh

git remote -v|grep code.csdn.net >/dev/null 2>&1
ret=$?; if [[ 0 -ne $ret ]]; then 
    first_checkout "CSDN" \
        "git@code.csdn.net:winlinvip/srs-csdn.git" \
        "srs-csdn" "$work_dir/scripts/csdn.mirror.sh"
    exit 0; 
fi 

sync_master
sync_1_0_release
sync_push "CSDN"

exit 0
