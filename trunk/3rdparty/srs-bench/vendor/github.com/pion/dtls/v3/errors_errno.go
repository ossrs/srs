// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build aix || darwin || dragonfly || freebsd || linux || nacl || nacljs || netbsd || openbsd || solaris || windows
// +build aix darwin dragonfly freebsd linux nacl nacljs netbsd openbsd solaris windows

// For systems having syscall.Errno.
// Update build targets by following command:
// $ grep -R ECONN $(go env GOROOT)/src/syscall/zerrors_*.go \
//     | tr "." "_" | cut -d"_" -f"2" | sort | uniq

package dtls

import (
	"errors"
	"os"
	"syscall"
)

func isOpErrorTemporary(err *os.SyscallError) bool {
	return errors.Is(err.Err, syscall.ECONNREFUSED)
}
