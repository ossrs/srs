#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

cmd="nohup python ./research/api-server/server.py 8085 >./objs/logs/api-server.log 2>&1 &"
echo "启动API服务器：$cmd"
pids=`ps aux|grep python|grep research|grep "api-server"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup python ./research/api-server/server.py 8085 >./objs/logs/api-server.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动API服务器失败"; exit $ret; fi

echo "启动API服务器成功"
exit 0
