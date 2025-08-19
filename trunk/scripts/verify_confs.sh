#!/bin/bash

TRUNK_DIR=$(dirname $(realpath -q $0))/..

pushd $TRUNK_DIR > /dev/null

SRS_EXE=$(pwd)/objs/srs

if [ ! -f ${SRS_EXE} ]; then
    echo "${SRS_EXE} not exist"
    exit -1
fi

if [ ! -x ${SRS_EXE} ]; then
    echo "${SRS_EXE} not executable"
    exit -2
fi

for f in conf/*.conf
do
    if [ -f $f ]; then
        # skip below conf
        if [[ $f == "conf/full.conf" ||
                  $f == "conf/hls.edge.conf" ||
                  $f == "conf/nginx.proxy.conf" ||
                  $f == "conf/include.vhost.conf" ]]; then
            continue
        fi
        
        ${SRS_EXE} -t -c $f
        RET=$?
        if [ $RET -ne 0 ]; then
            echo "please check $f"
            popd > /dev/null
            exit $RET
        fi
    fi
done

popd > /dev/null
