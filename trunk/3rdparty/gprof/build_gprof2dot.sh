#!/bin/bash

# check exists.
if [[ -d graphviz-2.18 ]]; then 
    echo "graphviz is ok";
    exit 0;
fi

# check sudoer.
sudo echo "ok" > /dev/null 2>&1; 
ret=$?; if [[ 0 -ne ${ret} ]]; then echo "you must be sudoer"; exit 1; fi

unzip -q graphviz-2.36.0.zip
cd graphviz-2.36.0 && ./configure && make && sudo make install
ret=$?; if [[ $ret -ne 0 ]]; then echo "build gprof2dot failed."; exit $ret; fi

echo "we test in Centos6.0, it's ok"
