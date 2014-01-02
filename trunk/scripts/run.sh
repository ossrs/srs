#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# linux shell color support.
RED="\\e[31m"
GREEN="\\e[32m"
YELLOW="\\e[33m"
BLACK="\\e[0m"

# step 1: build srs 
#bash scripts/_step.build.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 2: start srs 
bash scripts/_step.start.srs.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 3(optinal): start srs listen at 19350 to forward to
bash scripts/_step.start.srs.19350.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 4(optinal): start nginx for HLS 
bash scripts/_step.start.nginx.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 5(optinal): start http hooks for srs callback 
bash scripts/_step.start.api.server.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 6: publish demo live stream 
bash scripts/_step.start.ffmpeg.demo.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 7: publish players live stream 
bash scripts/_step.start.ffmpeg.players.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 8: add server ip to client hosts as demo. 
ip=`ifconfig|grep "inet"|grep "addr"|grep "Mask"|grep -v "127.0.0.1"|awk 'NR==1 {print $2}'|awk -F ':' '{print $2}'`
cat<<END
默认的12路流演示：
    http://$ip/players
默认的播放器流演示：
    http://$ip/players/srs_player.html?vhost=players
推流（主播）应用演示：
    http://$ip/players/srs_publisher.html?vhost=players
视频会议（聊天室）应用演示：
    http://$ip/players/srs_chat.html?vhost=players
默认的测速应用演示:
	http://$ip/players/srs_bwt.html?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com
END
echo -e "${GREEN}演示地址：${BLACK}"
echo -e "${RED}    http://$ip${BLACK}"
echo -e "${RED}    http://$ip:8085${BLACK}"
