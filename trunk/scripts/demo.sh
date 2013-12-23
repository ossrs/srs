#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# step 1: build srs 
echo "编译SRS"
./configure --with-ssl --with-hls --with-ffmpeg --with-http && make
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：编译SRS失败"; exit $ret; fi

# step 2: start srs 
cmd="nohup ./objs/srs -c conf/srs.conf >./objs/logs/srs.log 2>&1 &"
echo "启动SRS服务器：$cmd"
pids=`ps aux|grep srs|grep "./objs"|grep "srs.conf"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup ./objs/srs -c conf/srs.conf >./objs/logs/srs.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动SRS失败"; exit $ret; fi

# step 3(optinal): start srs listen at 19350 to forward to
cmd="nohup ./objs/srs -c conf/srs.19350.conf > ./objs/logs/srs.19350.log 2>&1 &"
echo "启动SRS转发服务器：$cmd"
pids=`ps aux|grep srs|grep "./objs"|grep "srs.19350.conf"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup ./objs/srs -c conf/srs.19350.conf > ./objs/logs/srs.19350.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动SRS转发服务器失败"; exit $ret; fi

# step 4(optinal): start nginx for HLS 
cmd="sudo ./objs/nginx/sbin/nginx"
echo "启动NGINX（HLS服务）：$cmd"
pids=`ps aux|grep nginx|grep process|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; sudo kill -s SIGKILL $pid; done
sudo ./objs/nginx/sbin/nginx
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动NGINX（HLS服务）失败"; exit $ret; fi

# step 5(optinal): start http hooks for srs callback 
cmd="nohup python ./research/api-server/server.py 8085 >./objs/logs/api-server.log 2>&1 &"
echo "启动API服务器：$cmd"
pids=`ps aux|grep python|grep research|grep "api-server"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup python ./research/api-server/server.py 8085 >./objs/logs/api-server.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动API服务器失败"; exit $ret; fi

# step 6: publish demo live stream 
cmd="nohup bash scripts/ffmpeg.demo.sh >./objs/logs/ffmpeg-demo.log 2>&1 &"
echo "启动FFMPEG推送demo流(播放器上12路演示)：$cmd"
pids=`ps aux|grep scripts|grep "ffmpeg.demo.sh"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup bash scripts/ffmpeg.demo.sh >./objs/logs/ffmpeg-demo.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动FFMPEG推送demo流(播放器上12路演示)失败"; exit $ret; fi

# step 7: publish players live stream 
cmd="nohup bash scripts/ffmpeg.players.sh >./objs/logs/ffmpeg-players.log 2>&1 &"
echo "启动FFMPEG推送players流(播放器上演示用)：$cmd"
pids=`ps aux|grep scripts|grep "ffmpeg.players.sh"|awk '{print $2}'`; for pid in $pids; do echo "结束现有进程：$pid"; kill -s SIGKILL $pid; done
nohup bash scripts/ffmpeg.players.sh >./objs/logs/ffmpeg-players.log 2>&1 &
ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动FFMPEG推送players流(播放器上演示用)失败"; exit $ret; fi

# step 8: add server ip to client hosts as demo. 
cat<<END
SRS系统启动成功，您需要在客户端机器设置hosts后即可观看演示：
    # edit the folowing file:
    # linux: /etc/hosts
    # windows: C:\Windows\System32\drivers\etc\hosts
    # where server ip is 192.168.2.111
    192.168.2.111 demo.srs.com
默认的12路流演示：http://demo.srs.com/players
默认的播放器流演示：http://demo.srs.com/players/srs_player.html?vhost=players
推流（主播）应用演示：http://demo.srs.com/players/srs_publisher.html?vhost=players
视频会议（聊天室）应用演示：http://demo.srs.com/players/srs_chat.html?vhost=players
END
