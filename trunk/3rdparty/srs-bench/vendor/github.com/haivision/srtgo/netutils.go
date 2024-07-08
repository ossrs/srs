package srtgo

//#include <srt/srt.h>
import "C"

import (
	"encoding/binary"
	"fmt"
	"net"
	"syscall"
	"unsafe"
)

func ntohs(val uint16) uint16 {
	tmp := ((*[unsafe.Sizeof(val)]byte)(unsafe.Pointer(&val)))
	return binary.BigEndian.Uint16((*tmp)[:])
}

func udpAddrFromSockaddr(addr *syscall.RawSockaddrAny) (*net.UDPAddr, error) {
	var udpAddr net.UDPAddr

	switch addr.Addr.Family {
	case afINET6:
		ptr := (*syscall.RawSockaddrInet6)(unsafe.Pointer(addr))
		udpAddr.Port = int(ntohs(ptr.Port))
		udpAddr.IP = ptr.Addr[:]

	case afINET4:
		ptr := (*syscall.RawSockaddrInet4)(unsafe.Pointer(addr))
		udpAddr.Port = int(ntohs(ptr.Port))
		udpAddr.IP = net.IPv4(
			ptr.Addr[0],
			ptr.Addr[1],
			ptr.Addr[2],
			ptr.Addr[3],
		)
	default:
		return nil, fmt.Errorf("unknown address family: %v", addr.Addr.Family)
	}

	return &udpAddr, nil
}

func sockAddrFromIp4(ip net.IP, port uint16) (*C.struct_sockaddr, int, error) {
	var raw syscall.RawSockaddrInet4
	raw.Family = afINET4

	p := (*[2]byte)(unsafe.Pointer(&raw.Port))
	p[0] = byte(port >> 8)
	p[1] = byte(port)

	copy(raw.Addr[:], ip.To4())

	return (*C.struct_sockaddr)(unsafe.Pointer(&raw)), int(sizeofSockAddrInet4), nil
}

func sockAddrFromIp6(ip net.IP, port uint16) (*C.struct_sockaddr, int, error) {
	var raw syscall.RawSockaddrInet6
	raw.Family = afINET6

	p := (*[2]byte)(unsafe.Pointer(&raw.Port))
	p[0] = byte(port >> 8)
	p[1] = byte(port)

	copy(raw.Addr[:], ip.To16())

	return (*C.struct_sockaddr)(unsafe.Pointer(&raw)), int(sizeofSockAddrInet6), nil
}

func CreateAddrInet(name string, port uint16) (*C.struct_sockaddr, int, error) {
	ip := net.ParseIP(name)
	if ip == nil {
		ips, err := net.LookupIP(name)
		if err != nil {
			return nil, 0, fmt.Errorf("Error in CreateAddrInet, LookupIP")
		}
		ip = ips[0]
	}

	if ip.To4() != nil {
		return sockAddrFromIp4(ip, port)
	} else if ip.To16() != nil {
		return sockAddrFromIp6(ip, port)
	}

	return nil, 0, fmt.Errorf("Error in CreateAddrInet, LookupIP")
}
