#!/bin/bash

./etc/init.d/srs-demo stop; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：停止SRS失败"; exit $ret; fi
echo "停止SRS服务器成功"

./etc/init.d/srs-demo-19350 stop; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：停止SRS转发服务器失败"; exit $ret; fi
echo "停止SRS转发服务器成功"

./etc/init.d/srs-api stop; ret=$?; if [[ 0 -ne $ret ]]; then echo "错误：停止API服务器失败"; exit $ret; fi
echo "停止API服务器成功"

echo "SRS系统服务均已停止"
