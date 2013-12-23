#!/bin/bash
src_dir='src'
if [[ ! -d $src_dir ]]; then echo "错误：必须在src同目录执行脚本"; exit 1; fi

# step 1: build srs 
bash scripts/_step.build.sh; ret=$?; if [[ 0 -ne $ret ]]; then exit $ret; fi

echo "编译SRS成功"
