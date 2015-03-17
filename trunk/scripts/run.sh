#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# linux shell color support.
RED="\\033[31m"
GREEN="\\033[32m"
YELLOW="\\033[33m"
BLACK="\\033[0m"

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

which getenforce >/dev/null 2>&1
if [[ 0 -eq $? && `getenforce` != 'Disabled' ]]; then
	echo -e "${RED}请关闭selinux：${BLACK}";
	echo -e "${RED}    打开配置文件：sudo vi /etc/sysconfig/selinux${BLACK}";
	echo -e "${RED}    修改为：SELINUX=disabled${BLACK}";
	echo -e "${RED}    重启系统：sudo reboot${BLACK}";
fi

if [[ -f /etc/init.d/iptables ]]; then
	sudo /etc/init.d/iptables status >/dev/null 2>&1;
	if [[ $? -ne 3 ]]; then
		echo -e "${RED}请关闭防火墙：${BLACK}";
		echo -e "${RED}    sudo /etc/init.d/iptables stop${BLACK}";
	fi
fi

echo -e "${GREEN}请在hosts中添加一行：${BLACK}"
echo -e "${RED}    $ip demo.srs.com${BLACK}"
echo -e "${GREEN}演示地址：${BLACK}"
echo -e "${RED}    http://$ip:$port${BLACK}"
echo -e "@see https://github.com/winlinvip/simple-rtmp-server/wiki/v1_CN_SampleDemo"
