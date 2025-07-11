#!/bin/env bash

# SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
# SPDX-License-Identifier: MIT

cp -v $GOROOT/src/crypto/hmac/{hmac,hmac_test}.go .
git diff {hmac,hmac_test}.go
