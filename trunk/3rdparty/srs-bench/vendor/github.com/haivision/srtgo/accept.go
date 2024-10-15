package srtgo

/*
#cgo LDFLAGS: -lsrt
#include <srt/srt.h>

SRTSOCKET srt_accept_wrapped(SRTSOCKET lsn, struct sockaddr* addr, int* addrlen, int *srterror, int *syserror)
{
	int ret = srt_accept(lsn, addr, addrlen);
	if (ret < 0) {
		*srterror = srt_getlasterror(syserror);
	}
	return ret;
}

*/
import "C"
import (
	"fmt"
	"net"
	"syscall"
	"unsafe"
)

func srtAcceptImpl(lsn C.SRTSOCKET, addr *C.struct_sockaddr, addrlen *C.int) (C.SRTSOCKET, error) {
	srterr := C.int(0)
	syserr := C.int(0)
	socket := C.srt_accept_wrapped(lsn, addr, addrlen, &srterr, &syserr)
	if srterr != 0 {
		srterror := SRTErrno(srterr)
		if syserr < 0 {
			srterror.wrapSysErr(syscall.Errno(syserr))
		}
		return socket, srterror
	}
	return socket, nil
}

// Accept an incoming connection
func (s SrtSocket) Accept() (*SrtSocket, *net.UDPAddr, error) {
	var err error
	if !s.blocking {
		err = s.pd.wait(ModeRead)
		if err != nil {
			return nil, nil, err
		}
	}
	var addr syscall.RawSockaddrAny
	sclen := C.int(syscall.SizeofSockaddrAny)
	socket, err := srtAcceptImpl(s.socket, (*C.struct_sockaddr)(unsafe.Pointer(&addr)), &sclen)
	if err != nil {
		return nil, nil, err
	}
	if socket == SRT_INVALID_SOCK {
		return nil, nil, fmt.Errorf("srt accept, error accepting the connection: %w", srtGetAndClearError())
	}

	newSocket, err := newFromSocket(&s, socket)
	if err != nil {
		return nil, nil, fmt.Errorf("new socket could not be created: %w", err)
	}

	udpAddr, err := udpAddrFromSockaddr(&addr)
	if err != nil {
		return nil, nil, err
	}

	return newSocket, udpAddr, nil
}
