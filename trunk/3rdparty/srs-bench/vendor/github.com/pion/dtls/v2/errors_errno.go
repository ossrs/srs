// +build aix darwin dragonfly freebsd linux nacl nacljs netbsd openbsd solaris windows

// For systems having syscall.Errno.
// Update build targets by following command:
// $ grep -R ECONN $(go env GOROOT)/src/syscall/zerrors_*.go \
//     | tr "." "_" | cut -d"_" -f"2" | sort | uniq

package dtls

import (
	"os"
	"syscall"
)

func isOpErrorTemporary(err *os.SyscallError) bool {
	if ne, ok := err.Err.(syscall.Errno); ok {
		switch ne {
		case syscall.ECONNREFUSED:
			return true
		default:
			return false
		}
	}
	return false
}
