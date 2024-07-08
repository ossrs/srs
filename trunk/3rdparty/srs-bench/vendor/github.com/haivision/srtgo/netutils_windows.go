//go:build windows

package srtgo

import (
	"unsafe"

	"golang.org/x/sys/windows"
)

const (
	afINET4 = windows.AF_INET
	afINET6 = windows.AF_INET6
)

var (
	sizeofSockAddrInet4 uint64 = 0
	sizeofSockAddrInet6 uint64 = 0
	sizeofSockaddrAny   uint64 = 0
)

func init() {
	inet4 := windows.RawSockaddrInet4{}
	inet6 := windows.RawSockaddrInet6{}
	any := windows.RawSockaddrAny{}
	sizeofSockAddrInet4 = uint64(unsafe.Sizeof(inet4))
	sizeofSockAddrInet6 = uint64(unsafe.Sizeof(inet6))
	sizeofSockaddrAny = uint64(unsafe.Sizeof(any))
}
