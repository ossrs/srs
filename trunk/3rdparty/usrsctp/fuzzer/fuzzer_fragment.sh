#!/usr/bin/env bash
export ASAN_OPTIONS=abort_on_error=1:disable_core=0:unmap_shadow_on_exit=1:disable_coredump=0
ulimit -c unlimited
mkdir -p CORPUS_LISTEN
./fuzzer_fragment -jobs=32 -timeout=10 -max_len=4086 CORPUS_FRAGMENT
