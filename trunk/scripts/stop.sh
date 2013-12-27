#!/bin/bash

echo "停止SRS服务器"
ps aux|grep srs|grep "./objs"|grep "srs.conf"
pids=`ps aux|grep srs|grep "./objs"|grep "srs.conf"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done

echo "停止SRS转发服务器"
ps aux|grep srs|grep "./objs"|grep "srs.19350.conf"
pids=`ps aux|grep srs|grep "./objs"|grep "srs.19350.conf"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done

# step 4(optinal): start nginx for HLS 
echo "停止NGINX（HLS服务）"
ps aux|grep nginx|grep process
if [[ -f ./objs/nginx/logs/nginx.pid ]]; then 
    pids=`cat ./objs/nginx/logs/nginx.pid`; for pid in $pids; do echo "结束现有进程：$pid"; sudo kill -s SIGTERM $pid; done
fi

# step 5(optinal): start http hooks for srs callback 
echo "停止API服务器"
ps aux|grep python|grep research|grep "api-server"
pids=`ps aux|grep python|grep research|grep "api-server"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done

# step 6: publish demo live stream 
echo "停止FFMPEG推送demo流(播放器上12路演示)"
ps aux|grep scripts|grep "ffmpeg.demo.sh"
pids=`ps aux|grep scripts|grep "/ffmpeg.demo.sh"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done

# step 7: publish players live stream 
echo "停止FFMPEG推送players流(播放器上演示用)"
ps aux|grep scripts|grep "ffmpeg.players.sh"
pids=`ps aux|grep scripts|grep "/ffmpeg.players.sh"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done

echo "SRS系统服务均已停止"
