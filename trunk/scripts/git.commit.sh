#!/bin/bash

cat <<END >>/dev/null
touch git-ensure-commit &&
echo "cd `pwd` &&" >git-ensure-commit &&
echo "bash `pwd`/git.commit.sh" >>git-ensure-commit &&
chmod +x git-ensure-commit &&
sudo rm -f /bin/git-ensure-commit &&
sudo mv git-ensure-commit /usr/local/bin/git-ensure-commit
END

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
cd $work_dir && work_branch=`git branch|grep "*"|awk '{print $2}'` && commit_branch=2.0release && git checkout $commit_branch
ret=$ret; if [[ $ret -ne 0 ]]; then echo "switch branch failed. ret=$ret"; exit $ret; fi
echo "work branch is $work_branch"
echo "commit branch is $commit_branch"

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
remote_check origin git@github.com:simple-rtmp-server/srs.git
remote_check srs.winlin git@github.com:winlinvip/simple-rtmp-server.git
remote_check srs.csdn git@code.csdn.net:winlinvip/srs-csdn.git
remote_check srs.oschina git@git.oschina.net:winlinvip/srs.oschina.git
remote_check srs.gitlab git@gitlab.com:winlinvip/srs-gitlab.git
ok_msg "remote check ok"

function sync_push()
{
    for ((;;)); do 
        git push $*
        ret=$?; if [[ 0 -ne $ret ]]; then 
            failed_msg "Retry for failed: git push $*"
            sleep 3
            continue
        else
            ok_msg "Success: git push $*"
        fi
        break
    done
}

sync_push origin
sync_push srs.winlin
sync_push srs.csdn
sync_push srs.oschina
sync_push srs.gitlab
ok_msg "push branches ok"

sync_push --tags srs.winlin
sync_push --tags srs.csdn
sync_push --tags srs.oschina
sync_push --tags srs.gitlab
ok_msg "push tags ok"

git checkout $work_branch
ok_msg "switch to work branch $work_branch"

exit 0
