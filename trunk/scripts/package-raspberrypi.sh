#!/bin/bash

echo "在RaspberryPi下直接打包，不编译直接打包（编译慢，手工编译）"

echo "argv[0]=$0"
if [[ ! -f $0 ]]; then 
    echo "directly execute the scripts on shell.";
    work_dir=`pwd`
else 
    echo "execute scripts in file: $0";
    work_dir=`dirname $0`; work_dir=`(cd ${work_dir} && pwd)`
fi

bash ${work_dir}/package.sh --no-build
