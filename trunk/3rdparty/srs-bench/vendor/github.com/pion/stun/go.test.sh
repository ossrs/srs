#!/usr/bin/env bash

set -e
touch coverage.txt

# test fuzz inputs
go test -tags gofuzz -run TestFuzz -v .

# quick-test without -race
go test ./...

# test with "debug" tag
go test -tags debug ./...

# test concurrency
go test -race -cpu=1,2,4 -run TestClient_DoConcurrent

for d in $(go list ./... | grep -v vendor); do
    go test -race -coverprofile=profile.out -covermode=atomic "$d"
    if [[ -f profile.out ]]; then
        cat profile.out >> coverage.txt
        rm profile.out
    fi
done
