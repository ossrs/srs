// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT
package bootstrap

import (
	"context"
)

type Bootstrap interface {
	Start(ctx context.Context) error
}
