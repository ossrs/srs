//go:build !windows

package srtgo

import (
	"syscall"

	"golang.org/x/sys/unix"
)

const (
	sizeofSockAddrInet4 = syscall.SizeofSockaddrInet4
	sizeofSockAddrInet6 = syscall.SizeofSockaddrInet6
	sizeofSockaddrAny   = syscall.SizeofSockaddrAny
	afINET4             = unix.AF_INET
	afINET6             = unix.AF_INET6
)
