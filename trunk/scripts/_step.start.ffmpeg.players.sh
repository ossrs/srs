#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

cmd="nohup bash scripts/ffmpeg.players.sh >./objs/logs/ffmpeg-players.log 2>&1 &"
echo "启动FFMPEG推送players流(播放器上演示用)：$cmd"
pids=`ps aux|grep scripts|grep "/ffmpeg.players.sh"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup bash scripts/ffmpeg.players.sh >./objs/logs/ffmpeg-players.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动FFMPEG推送players流(播放器上演示用)失败"; exit $ret; fi

echo "启动FFMPEG推送players流(播放器上演示用)成功"
exit 0
