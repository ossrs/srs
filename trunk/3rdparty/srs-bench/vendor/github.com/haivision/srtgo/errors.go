package srtgo

/*
#cgo LDFLAGS: -lsrt
#include <srt/srt.h>
*/
import "C"
import (
	"strconv"
	"syscall"
)

type SrtInvalidSock struct{}
type SrtRendezvousUnbound struct{}
type SrtSockConnected struct{}
type SrtConnectionRejected struct{}
type SrtConnectTimeout struct{}
type SrtSocketClosed struct{}
type SrtEpollTimeout struct{}

func (m *SrtInvalidSock) Error() string {
	return "Socket u indicates no valid socket ID"
}

func (m *SrtRendezvousUnbound) Error() string {
	return "Socket u is in rendezvous mode, but it wasn't bound"
}

func (m *SrtSockConnected) Error() string {
	return "Socket u is already connected"
}

func (m *SrtConnectionRejected) Error() string {
	return "Connection has been rejected"
}

func (m *SrtConnectTimeout) Error() string {
	return "Connection has been timed out"
}

func (m *SrtSocketClosed) Error() string {
	return "The socket has been closed"
}

func (m *SrtEpollTimeout) Error() string {
	return "Operation has timed out"
}

func (m *SrtEpollTimeout) Timeout() bool {
	return true
}

func (m *SrtEpollTimeout) Temporary() bool {
	return true
}

//MUST be called from same OS thread that generated the error (i.e.: use runtime.LockOSThread())
func srtGetAndClearError() error {
	defer C.srt_clearlasterror()
	eSysErrno := C.int(0)
	errno := C.srt_getlasterror(&eSysErrno)
	srterr := SRTErrno(errno)
	if eSysErrno != 0 {
		return srterr.wrapSysErr(syscall.Errno(eSysErrno))
	}
	return srterr
}

//Based of off golang errno handling: https://cs.opensource.google/go/go/+/refs/tags/go1.16.6:src/syscall/syscall_unix.go;l=114
type SRTErrno int

func (e SRTErrno) Error() string {
	//Workaround for unknown being -1
	if e == Unknown {
		return "Internal error when setting the right error code"
	}
	if 0 <= int(e) && int(e) < len(srterrors) {
		s := srterrors[e]
		if s != "" {
			return s
		}
	}
	return "srterrno: " + strconv.Itoa(int(e))
}

func (e SRTErrno) Is(target error) bool {
	//for backwards compat
	switch target.(type) {
	case *SrtInvalidSock:
		return e == EInvSock
	case *SrtRendezvousUnbound:
		return e == ERdvUnbound
	case *SrtSockConnected:
		return e == EConnSock
	case *SrtConnectionRejected:
		return e == EConnRej
	case *SrtConnectTimeout:
		return e == ETimeout
	case *SrtSocketClosed:
		return e == ESClosed
	}
	return false
}

func (e SRTErrno) Temporary() bool {
	return e == EAsyncFAIL || e == EAsyncRCV || e == EAsyncSND || e == ECongest || e == ETimeout
}

func (e SRTErrno) Timeout() bool {
	return e == ETimeout
}

func (e SRTErrno) wrapSysErr(errno syscall.Errno) error {
	return &srtErrnoSysErrnoWrapped{
		e:    e,
		eSys: errno,
	}
}

type srtErrnoSysErrnoWrapped struct {
	e    SRTErrno
	eSys syscall.Errno
}

func (e *srtErrnoSysErrnoWrapped) Error() string {
	return e.e.Error()
}

func (e *srtErrnoSysErrnoWrapped) Is(target error) bool {
	return e.e.Is(target)
}

func (e *srtErrnoSysErrnoWrapped) Temporary() bool {
	return e.e.Temporary()
}

func (e *srtErrnoSysErrnoWrapped) Timeout() bool {
	return e.e.Timeout()
}

func (e *srtErrnoSysErrnoWrapped) Unwrap() error {
	return error(e.eSys)
}

//Shadows SRT_ERRNO srtcore/srt.h line 490+
const (
	Unknown = SRTErrno(C.SRT_EUNKNOWN)
	Success = SRTErrno(C.SRT_SUCCESS)
	//Major: SETUP
	EConnSetup = SRTErrno(C.SRT_ECONNSETUP)
	ENoServer  = SRTErrno(C.SRT_ENOSERVER)
	EConnRej   = SRTErrno(C.SRT_ECONNREJ)
	ESockFail  = SRTErrno(C.SRT_ESOCKFAIL)
	ESecFail   = SRTErrno(C.SRT_ESECFAIL)
	ESClosed   = SRTErrno(C.SRT_ESCLOSED)
	//Major: CONNECTION
	EConnFail = SRTErrno(C.SRT_ECONNFAIL)
	EConnLost = SRTErrno(C.SRT_ECONNLOST)
	ENoConn   = SRTErrno(C.SRT_ENOCONN)
	//Major: SYSTEMRES
	EResource = SRTErrno(C.SRT_ERESOURCE)
	EThread   = SRTErrno(C.SRT_ETHREAD)
	EnoBuf    = SRTErrno(C.SRT_ENOBUF)
	ESysObj   = SRTErrno(C.SRT_ESYSOBJ)
	//Major: FILESYSTEM
	EFile     = SRTErrno(C.SRT_EFILE)
	EInvRdOff = SRTErrno(C.SRT_EINVRDOFF)
	ERdPerm   = SRTErrno(C.SRT_ERDPERM)
	EInvWrOff = SRTErrno(C.SRT_EINVWROFF)
	EWrPerm   = SRTErrno(C.SRT_EWRPERM)
	//Major: NOTSUP
	EInvOp          = SRTErrno(C.SRT_EINVOP)
	EBoundSock      = SRTErrno(C.SRT_EBOUNDSOCK)
	EConnSock       = SRTErrno(C.SRT_ECONNSOCK)
	EInvParam       = SRTErrno(C.SRT_EINVPARAM)
	EInvSock        = SRTErrno(C.SRT_EINVSOCK)
	EUnboundSock    = SRTErrno(C.SRT_EUNBOUNDSOCK)
	ENoListen       = SRTErrno(C.SRT_ENOLISTEN)
	ERdvNoServ      = SRTErrno(C.SRT_ERDVNOSERV)
	ERdvUnbound     = SRTErrno(C.SRT_ERDVUNBOUND)
	EInvalMsgAPI    = SRTErrno(C.SRT_EINVALMSGAPI)
	EInvalBufferAPI = SRTErrno(C.SRT_EINVALBUFFERAPI)
	EDupListen      = SRTErrno(C.SRT_EDUPLISTEN)
	ELargeMsg       = SRTErrno(C.SRT_ELARGEMSG)
	EInvPollID      = SRTErrno(C.SRT_EINVPOLLID)
	EPollEmpty      = SRTErrno(C.SRT_EPOLLEMPTY)
	//EBindConflict   = SRTErrno(C.SRT_EBINDCONFLICT)
	//Major: AGAIN
	EAsyncFAIL = SRTErrno(C.SRT_EASYNCFAIL)
	EAsyncSND  = SRTErrno(C.SRT_EASYNCSND)
	EAsyncRCV  = SRTErrno(C.SRT_EASYNCRCV)
	ETimeout   = SRTErrno(C.SRT_ETIMEOUT)
	ECongest   = SRTErrno(C.SRT_ECONGEST)
	//Major: PEERERROR
	EPeer = SRTErrno(C.SRT_EPEERERR)
)

//Unknown cannot be here since it would have a negative index!
//Error strings taken from: https://github.com/Haivision/srt/blob/master/docs/API/API-functions.md
var srterrors = [...]string{
	Success:         "The value set when the last error was cleared and no error has occurred since then",
	EConnSetup:      "General setup error resulting from internal system state",
	ENoServer:       "Connection timed out while attempting to connect to the remote address",
	EConnRej:        "Connection has been rejected",
	ESockFail:       "An error occurred when trying to call a system function on an internally used UDP socket",
	ESecFail:        "A possible tampering with the handshake packets was detected, or encryption request wasn't properly fulfilled.",
	ESClosed:        "A socket that was vital for an operation called in blocking mode has been closed during the operation",
	EConnFail:       "General connection failure of unknown details",
	EConnLost:       "The socket was properly connected, but the connection has been broken",
	ENoConn:         "The socket is not connected",
	EResource:       "System or standard library error reported unexpectedly for unknown purpose",
	EThread:         "System was unable to spawn a new thread when requried",
	EnoBuf:          "System was unable to allocate memory for buffers",
	ESysObj:         "System was unable to allocate system specific objects",
	EFile:           "General filesystem error (for functions operating with file transmission)",
	EInvRdOff:       "Failure when trying to read from a given position in the file",
	ERdPerm:         "Read permission was denied when trying to read from file",
	EInvWrOff:       "Failed to set position in the written file",
	EWrPerm:         "Write permission was denied when trying to write to a file",
	EInvOp:          "Invalid operation performed for the current state of a socket",
	EBoundSock:      "The socket is currently bound and the required operation cannot be performed in this state",
	EConnSock:       "The socket is currently connected and therefore performing the required operation is not possible",
	EInvParam:       "Call parameters for API functions have some requirements that were not satisfied",
	EInvSock:        "The API function required an ID of an entity (socket or group) and it was invalid",
	EUnboundSock:    "The operation to be performed on a socket requires that it first be explicitly bound",
	ENoListen:       "The socket passed for the operation is required to be in the listen state",
	ERdvNoServ:      "The required operation cannot be performed when the socket is set to rendezvous mode",
	ERdvUnbound:     "An attempt was made to connect to a socket set to rendezvous mode that was not first bound",
	EInvalMsgAPI:    "The function was used incorrectly in the message API",
	EInvalBufferAPI: "The function was used incorrectly in the stream (buffer) API",
	EDupListen:      "The port tried to be bound for listening is already busy",
	ELargeMsg:       "Size exceeded",
	EInvPollID:      "The epoll ID passed to an epoll function is invalid",
	EPollEmpty:      "The epoll container currently has no subscribed sockets",
	//EBindConflict:   "SRT_EBINDCONFLICT",
	EAsyncFAIL: "General asynchronous failure (not in use currently)",
	EAsyncSND:  "Sending operation is not ready to perform",
	EAsyncRCV:  "Receiving operation is not ready to perform",
	ETimeout:   "The operation timed out",
	ECongest:   "With SRTO_TSBPDMODE and SRTO_TLPKTDROP set to true, some packets were dropped by sender",
	EPeer:      "Receiver peer is writing to a file that the agent is sending",
}
