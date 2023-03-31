#!/usr/bin/env bash

#export ASAN_OPTIONS=abort_on_error=1:disable_core=0:unmap_shadow_on_exit=1:disable_coredump=0:detect_leaks=1
ulimit -c unlimited
mkdir -p CORPUS_CONNECT

NPROC=1

if [[ "$OSTYPE" == "linux-gnu" ]]; then
    NPROC=$(nproc)
elif [[ "$OSTYPE" == "darwin"* ]]; then
    NPROC=$(sysctl -n hw.ncpu)
elif [[ "$OSTYPE" == "freebsd"* ]]; then
    NPROC=$(sysctl -n hw.ncpu)
else
    exit 1
fi

echo "$NPROC"


./fuzzer_connect_multi -jobs=64 -timeout=10 -max_len=32000 -use_value_profile=1 CORPUS_CONNECT
