#!/bin/bash
cp -v $GOROOT/src/crypto/hmac/{hmac,hmac_test}.go .
git diff {hmac,hmac_test}.go

