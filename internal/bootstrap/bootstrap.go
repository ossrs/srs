// Copyright (c) 2025 Winlin
//
// SPDX-License-Identifier: MIT
package bootstrap

import (
	"context"
)

// Bootstrap defines the interface for application bootstrap operations.
type Bootstrap interface {
	// Start initializes the context with logger and signal handlers, then runs the bootstrap.
	// Returns any error encountered during startup.
	Start(ctx context.Context) error
}
