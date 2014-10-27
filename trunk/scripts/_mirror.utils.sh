#!/bin/bash

#############################################
# help for the first checkout.
#############################################
function first_checkout()
{
    mirror_name=$1
    git_url=$2
    project_dir=$3
    sync_script=$4
    
    failed_msg "当前分支不是${mirror_name}镜像"; 
    
    cat <<END
创建${mirror_name}镜像的过程如下：
1. 在${mirror_name}上创建项目，
        可创建空项目，或从https://github.com/winlinvip/simple-rtmp-server拷贝过来。
2. 在本地虚拟机上：    
        git clone $git_url
        cd $project_dir && git checkout master && git branch 1.0release && git push origin 1.0release
3. 创建同步的branch：    
        git remote add upstream https://github.com/winlinvip/simple-rtmp-server.git
        git fetch upstream    
        git checkout upstream/master -b srs.master
        git checkout upstream/1.0release -b srs.1.0release
4. 执行本同步更新脚本，更新。
        bash $sync_script
END
}

#############################################
# branch master
#############################################
function sync_master()
{
    for ((;;)); do
        git checkout srs.master && git pull 
        ret=$?; if [[ 0 -ne $ret ]]; then 
            failed_msg "(master)更新github分支失败，自动重试";
            continue
        else
            ok_msg "(master)更新github分支成功"
        fi
        break
    done

    git checkout master && git merge srs.master
    ret=$?; if [[ 0 -ne $ret ]]; then failed_msg "(master)合并github分支失败, ret=$ret"; exit $ret; fi
    ok_msg "(master)合并github分支成功"
}

#############################################
# branch 1.0release
#############################################
function sync_1_0_release()
{
    for ((;;)); do
        git checkout srs.1.0release && git pull 
        ret=$?; if [[ 0 -ne $ret ]]; then 
            failed_msg "(1.0release)更新github分支失败，自动重试";
            continue
        else
            ok_msg "(1.0release)更新github分支成功"
        fi
        break
    done

    git checkout 1.0release && git merge srs.1.0release
    ret=$?; if [[ 0 -ne $ret ]]; then failed_msg "(1.0release)合并github分支失败, ret=$ret"; exit $ret; fi
    ok_msg "(1.0release)合并github分支成功"
}

#############################################
# push
#############################################
function sync_push()
{
    mirror_name=$1
    
    for ((;;)); do 
        git push
        ret=$?; if [[ 0 -ne $ret ]]; then 
            failed_msg "提交${mirror_name}分支失败，自动重试";
            continue
        else
            ok_msg "提交${mirror_name}分支成功"
        fi
        break
    done

    git checkout master
    ok_msg "${mirror_name}同步git成功"
}
