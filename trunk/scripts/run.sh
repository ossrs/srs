#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# linux shell color support.
RED="\\e[31m"
GREEN="\\e[32m"
YELLOW="\\e[33m"
BLACK="\\e[0m"

./etc/init.d/srs-demo restart; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动SRS失败"; exit $ret; fi
echo "启动SRS服务器成功"

./etc/init.d/srs-demo-19350 restart; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动SRS转发服务器失败"; exit $ret; fi
echo "启动SRS转发服务器成功"

./etc/init.d/srs-api restart; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：启动API服务器失败"; exit $ret; fi
echo "启动API服务器成功"

ip=`ifconfig|grep "inet "|grep -v "127.0.0.1"|awk -F 'inet ' 'NR==1 {print $2}'|awk '{print $1}'|sed "s/addr://g"`
port=8085
cat<<END
默认的12路流演示：
    http://$ip:$port/players
默认的播放器流演示：
    http://$ip:$port/players/srs_player.html?vhost=players
推流（主播）应用演示：
    http://$ip:$port/players/srs_publisher.html?vhost=players
视频会议（聊天室）应用演示：
    http://$ip:$port/players/srs_chat.html?vhost=players
默认的测速应用演示:
    http://$ip:$port/players/srs_bwt.html?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com
END
echo -e "${GREEN}演示地址：${BLACK}"
echo -e "${RED}    http://$ip:$port${BLACK}"
