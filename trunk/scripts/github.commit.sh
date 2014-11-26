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

function remote_check()
{
    remote=$1
    url=$2
    git remote -v| grep "$url" >/dev/null 2>&1
    ret=$?; if [[ 0 -ne $ret ]]; then
        echo "remote $remote not found, add by:"
        echo "    git remote add $remote $url"
        exit -1
    fi
    ok_msg "remote $remote ok, url is $url"
}
remote_check origin git@github.com:winlinvip/simple-rtmp-server.git
remote_check srs.csdn git@code.csdn.net:winlinvip/srs-csdn.git
remote_check srs.oschina git@git.oschina.net:winlinvip/srs.oschina.git

function sync_push()
{
    repository=$1
    branch=$2
    
    for ((;;)); do 
        git push $repository $branch
        ret=$?; if [[ 0 -ne $ret ]]; then 
            failed_msg "提交$repository/$branch分支失败，自动重试";
            continue
        else
            ok_msg "提交$repository/$branch分支成功"
        fi
        break
    done
    ok_msg "$repository/$branch同步git成功"
}

sync_push origin master
sync_push origin 1.0release
sync_push srs.csdn master
sync_push srs.csdn 1.0release
sync_push srs.oschina master
sync_push srs.oschina 1.0release
ok_msg "sync push ok"

sync_push srs.csdn --tags
sync_push srs.oschina --tags
ok_msg "sync tags ok"

exit 0
