// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

//go:build !windows

package cases

import (
	"errors"
	"syscall"
)

const sigUsr1 = syscall.SIGUSR1

func connectRefused(err error) bool {
	return errors.Is(err, syscall.ECONNREFUSED)
}
