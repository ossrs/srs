#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# step 1: build srs 
#bash scripts/_step.build.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 2: start srs 
bash scripts/_step.start.srs.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 3(optinal): start srs listen at 19350 to forward to
#bash scripts/_step.start.srs.19350.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 4(optinal): start nginx for HLS 
bash scripts/_step.start.nginx.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 5(optinal): start http hooks for srs callback 
bash scripts/_step.start.api.server.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 6: publish demo live stream 
#bash scripts/_step.start.ffmpeg.demo.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 7: publish players live stream 
#bash scripts/_step.start.ffmpeg.players.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

# step 8: add server ip to client hosts as demo. 
cat<<END
SRS系统开发环境启动成功
默认的播放器流演示：http://dev/players
END
