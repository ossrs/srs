// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package cases

import "syscall"

// sigUsr1 is the Linux number: Windows has no SIGUSR1, and the cases that
// send it are on its skip list.
const sigUsr1 = syscall.Signal(10)

// connectRefused takes any failed connect as refused: Windows retries a
// refused connect to a local port for about 2s, so a short connect times out
// instead.
func connectRefused(err error) bool {
	return err != nil
}
