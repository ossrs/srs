#!/bin/bash

# sudo ln -sf `pwd`/git2unix.sh /bin/git2unix

dos2unix -V>/dev/null 2>&1
ret=$?; if [[ 0 -ne $ret ]]; then 
    echo "dos2unix not found." 
    echo "      sudo yum install -y dos2unix"
    exit $ret
fi

files=`git status|egrep "(modified|new file)"|awk -F ':' '{print $2}'|awk '{print $1}'|egrep "(.hpp$|.cpp$|.cc$|.h$|.c$|.txt$|.sh$)"`;
for file in $files; do
    dos2unix $file;
    echo $file|grep ".sh$" >/dev/null 2>&1; EOF_SH=$?
    if [[ $EOF_SH -ne 0 && -f $file ]]; then 
        echo "chmod -x $file"
        chmod -x $file; 
    fi
done
