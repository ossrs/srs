// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package main

import (
	"context"
	"os"

	"srsx/internal/bootstrap"
)

func main() {
	bs := bootstrap.NewBootstrap()
	if err := bs.Start(context.Background()); err != nil {
		// Error already logged in bootstrap.Start().
		os.Exit(-1)
	}
}
