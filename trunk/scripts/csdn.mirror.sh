#!/bin/bash

echo "更新CSDN镜像的脚本"

# 创建CSDN镜像的过程如下：
# 1. 在CSDN上创建项目，从https://github.com/winlinvip/simple-rtmp-server拷贝过来。
# 2. 在本地虚拟机上：git clone git@code.csdn.net:winlinvip/srs-csdn.git  
# 3. 创建同步的branch：
#       git remote add upstream https://github.com/winlinvip/simple-rtmp-server.git
#       git fetch upstream
#       git checkout upstream/master -b srs.master
# 4. 执行本同步更新脚本，更新。
#       bash scripts/csdn.mirror.sh

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

. ${product_dir}/scripts/_log.sh
ret=$?; if [[ $ret -ne 0 ]]; then exit $ret; fi
ok_msg "导入脚本成功"

for ((;;)); do
    git checkout srs.master && git pull
    ret=$?; if [[ 0 -ne $ret ]]; then 
        failed_msg "更新github分支失败，自动重试";
        continue
    else
        ok_msg "更新github分支成功"
    fi
    break
done

git checkout master && git merge srs.master
ret=$?; if [[ 0 -ne $ret ]]; then failed_msg "合并github分支失败, ret=$ret"; exit $ret; fi
ok_msg "合并github分支成功"

git commit -a -m "merge from github.srs"
ret=$?; if [[ 0 -ne $ret ]]; then 
    warn_msg "提交CSDN失败，忽略, ret=$ret";
else
    ok_msg "提交CSDN成功"
fi

for ((;;)); do 
    git push
    ret=$?; if [[ 0 -ne $ret ]]; then 
        failed_msg "提交CSDN分支失败，自动重试";
        continue
    else
        ok_msg "提交CSDN分支成功"
    fi
    break
done

ok_msg "CSDN同步git成功"

exit 0
