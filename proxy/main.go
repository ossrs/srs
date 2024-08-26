// Copyright (c) 2024 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"srs-proxy/log"
)

func main() {
	ctx := log.WithContext(context.Background())
	log.If(ctx, "SRS %v/%v started", Signature(), Version())
}
