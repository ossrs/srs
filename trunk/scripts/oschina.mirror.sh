#!/bin/bash

echo "更新OSChina镜像的脚本"

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

git remote -v|grep git.oschina.net >/dev/null 2>&1
ret=$?; if [[ 0 -ne $ret ]]; then 
    failed_msg "当前分支不是OSChina镜像"; 
    cat <<END
创建OSChina镜像的过程如下：
1. 在OSChina上创建项目，从https://github.com/winlinvip/simple-rtmp-server拷贝过来。
2. 在本地虚拟机上：    
      git clone git@git.oschina.net:winlinvip/srs.oschina.git
	  cd srs.oschina && git checkout master && git branch 1.0release && git push origin 1.0release
3. 创建同步的branch：    
      git remote add upstream https://github.com/winlinvip/simple-rtmp-server.git
      git fetch upstream    
      git checkout upstream/master -b srs.master
	  git checkout upstream/1.0release -b srs.1.0release
4. 执行本同步更新脚本，更新。
      bash scripts/oschina.mirror.sh
END
    exit 0; 
fi 

#############################################
# branch master
#############################################
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

#############################################
# branch 1.0release
#############################################
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

#############################################
# push
#############################################

for ((;;)); do 
    git push
    ret=$?; if [[ 0 -ne $ret ]]; then 
        failed_msg "提交OSChina分支失败，自动重试";
        continue
    else
        ok_msg "提交OSChina分支成功"
    fi
    break
done

git checkout master
ok_msg "OSChina同步git成功"

exit 0
