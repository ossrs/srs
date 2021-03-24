#!/bin/bash

FILES=(udpproxy.go udpproxy_test.go)
for file in ${FILES[@]}; do
  echo "cp vnet/udpproxy.go ~/git/transport/vnet/" &&
  cp vnet/udpproxy.go ~/git/transport/vnet/
done

# https://github.com/pion/webrtc/wiki/Contributing#run-all-automated-tests-and-checks-before-submitting
cd ~/git/transport/

echo ".github/lint-commit-message.sh" &&
.github/lint-commit-message.sh &&
echo ".github/assert-contributors.sh" &&
.github/assert-contributors.sh &&
echo ".github/lint-disallowed-functions-in-library.sh" &&
.github/lint-disallowed-functions-in-library.sh &&
echo ".github/lint-filename.sh" &&
.github/lint-filename.sh
if [[ $? -ne 0 ]]; then echo "fail"; exit -1; fi

# https://github.com/pion/webrtc/wiki/Contributing#run-all-automated-tests-and-checks-before-submitting
cd ~/git/transport/vnet/

echo "go test -race ./..." &&
go test -race ./...
if [[ $? -ne 0 ]]; then echo "fail"; exit -1; fi

echo "golangci-lint run --skip-files conn_map_test.go" &&
golangci-lint run --skip-files conn_map_test.go
if [[ $? -ne 0 ]]; then echo "fail"; exit -1; fi

echo "OK"
