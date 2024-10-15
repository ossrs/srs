package srtgo

/*
#cgo LDFLAGS: -lsrt
#include <srt/srt.h>
#include <srt/access_control.h>
#include "callback.h"
static const SRTSOCKET get_srt_invalid_sock() { return SRT_INVALID_SOCK; };
static const int get_srt_error() { return SRT_ERROR; };
static const int get_srt_error_reject_predefined() { return SRT_REJC_PREDEFINED; };
static const int get_srt_error_reject_userdefined() { return SRT_REJC_USERDEFINED; };
*/
import "C"

import (
	"errors"
	"fmt"
	"net"
	"runtime"
	"strconv"
	"sync"
	"syscall"
	"time"
	"unsafe"

	gopointer "github.com/mattn/go-pointer"
)

// SRT Socket mode
const (
	ModeFailure = iota
	ModeListener
	ModeCaller
	ModeRendezvouz
)

// Binding ops
const (
	bindingPre  = 0
	bindingPost = 1
)

// SrtSocket - SRT socket
type SrtSocket struct {
	socket      C.int
	blocking    bool
	pd          *pollDesc
	host        string
	port        uint16
	options     map[string]string
	mode        int
	pktSize     int
	pollTimeout int64
}

var (
	callbackMutex      sync.Mutex
	listenCallbackMap  map[C.int]unsafe.Pointer = make(map[C.int]unsafe.Pointer)
	connectCallbackMap map[C.int]unsafe.Pointer = make(map[C.int]unsafe.Pointer)
)

// Static consts from library
var (
	SRT_INVALID_SOCK = C.get_srt_invalid_sock()
	SRT_ERROR        = C.get_srt_error()
	SRTS_CONNECTED   = C.SRTS_CONNECTED
)

const defaultPacketSize = 1456

// InitSRT - Initialize srt library
func InitSRT() {
	C.srt_startup()
}

// CleanupSRT - Cleanup SRT lib
func CleanupSRT() {
	C.srt_cleanup()
}

// NewSrtSocket - Create a new SRT Socket
func NewSrtSocket(host string, port uint16, options map[string]string) *SrtSocket {
	s := new(SrtSocket)

	s.socket = C.srt_create_socket()
	if s.socket == SRT_INVALID_SOCK {
		return nil
	}

	s.host = host
	s.port = port
	s.options = options
	s.pollTimeout = -1

	val, exists := options["pktsize"]
	if exists {
		pktSize, err := strconv.Atoi(val)
		if err != nil {
			s.pktSize = pktSize
		}
	}
	if s.pktSize <= 0 {
		s.pktSize = defaultPacketSize
	}

	val, exists = options["blocking"]
	if exists && val != "0" {
		s.blocking = true
	}

	if !s.blocking {
		s.pd = pollDescInit(s.socket)
	}

	finalizer := func(obj interface{}) {
		sf := obj.(*SrtSocket)
		sf.Close()
		if sf.pd != nil {
			sf.pd.release()
		}
	}

	//Cleanup SrtSocket if no references exist anymore
	runtime.SetFinalizer(s, finalizer)

	var err error
	s.mode, err = s.preconfiguration()
	if err != nil {
		return nil
	}

	return s
}

func newFromSocket(acceptSocket *SrtSocket, socket C.SRTSOCKET) (*SrtSocket, error) {
	s := new(SrtSocket)
	s.socket = socket
	s.pktSize = acceptSocket.pktSize
	s.blocking = acceptSocket.blocking
	s.pollTimeout = acceptSocket.pollTimeout

	err := acceptSocket.postconfiguration(s)
	if err != nil {
		return nil, err
	}

	if !s.blocking {
		s.pd = pollDescInit(s.socket)
	}

	finalizer := func(obj interface{}) {
		sf := obj.(*SrtSocket)
		sf.Close()
		if sf.pd != nil {
			sf.pd.release()
		}
	}

	//Cleanup SrtSocket if no references exist anymore
	runtime.SetFinalizer(s, finalizer)

	return s, nil
}

func (s SrtSocket) GetSocket() C.int {
	return s.socket
}

// Listen for incoming connections. The backlog setting defines how many sockets
// may be allowed to wait until they are accepted (excessive connection requests
// are rejected in advance)
func (s *SrtSocket) Listen(backlog int) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	nbacklog := C.int(backlog)

	sa, salen, err := CreateAddrInet(s.host, s.port)
	if err != nil {
		return err
	}

	res := C.srt_bind(s.socket, sa, C.int(salen))
	if res == SRT_ERROR {
		C.srt_close(s.socket)
		return fmt.Errorf("Error in srt_bind: %w", srtGetAndClearError())
	}

	res = C.srt_listen(s.socket, nbacklog)
	if res == SRT_ERROR {
		C.srt_close(s.socket)
		return fmt.Errorf("Error in srt_listen: %w", srtGetAndClearError())
	}

	err = s.postconfiguration(s)
	if err != nil {
		return fmt.Errorf("Error setting post socket options")
	}

	return nil
}

// Connect to a remote endpoint
func (s *SrtSocket) Connect() error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	sa, salen, err := CreateAddrInet(s.host, s.port)
	if err != nil {
		return err
	}

	res := C.srt_connect(s.socket, sa, C.int(salen))
	if res == SRT_ERROR {
		C.srt_close(s.socket)
		return srtGetAndClearError()
	}

	if !s.blocking {
		if err := s.pd.wait(ModeWrite); err != nil {
			return err
		}
	}

	err = s.postconfiguration(s)
	if err != nil {
		return fmt.Errorf("Error setting post socket options in connect")
	}

	return nil
}

// Stats - Retrieve stats from the SRT socket
func (s SrtSocket) Stats() (*SrtStats, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	var stats C.SRT_TRACEBSTATS = C.SRT_TRACEBSTATS{}
	var b C.int = 1
	if C.srt_bstats(s.socket, &stats, b) == SRT_ERROR {
		return nil, fmt.Errorf("Error getting stats, %w", srtGetAndClearError())
	}

	return newSrtStats(&stats), nil
}

// Mode - Return working mode of the SRT socket
func (s SrtSocket) Mode() int {
	return s.mode
}

// PacketSize - Return packet size of the SRT socket
func (s SrtSocket) PacketSize() int {
	return s.pktSize
}

// PollTimeout - Return polling max time, for connect/read/write operations.
// Only applied when socket is in non-blocking mode.
func (s SrtSocket) PollTimeout() time.Duration {
	return time.Duration(s.pollTimeout) * time.Millisecond
}

// SetPollTimeout - Sets polling max time, for connect/read/write operations.
// Only applied when socket is in non-blocking mode.
func (s *SrtSocket) SetPollTimeout(pollTimeout time.Duration) {
	s.pollTimeout = pollTimeout.Milliseconds()
}

func (s *SrtSocket) SetDeadline(deadline time.Time) {
	s.pd.setDeadline(deadline, ModeRead+ModeWrite)
}

func (s *SrtSocket) SetReadDeadline(deadline time.Time) {
	s.pd.setDeadline(deadline, ModeRead)
}

func (s *SrtSocket) SetWriteDeadline(deadline time.Time) {
	s.pd.setDeadline(deadline, ModeWrite)
}

// Close the SRT socket
func (s *SrtSocket) Close() {

	C.srt_close(s.socket)
	s.socket = SRT_INVALID_SOCK
	if !s.blocking {
		s.pd.close()
	}
	callbackMutex.Lock()
	if ptr, exists := listenCallbackMap[s.socket]; exists {
		gopointer.Unref(ptr)
	}
	if ptr, exists := connectCallbackMap[s.socket]; exists {
		gopointer.Unref(ptr)
	}
	callbackMutex.Unlock()
}

// ListenCallbackFunc specifies a function to be called before a connecting socket is passed to accept
type ListenCallbackFunc func(socket *SrtSocket, version int, addr *net.UDPAddr, streamid string) bool

//export srtListenCBWrapper
func srtListenCBWrapper(arg unsafe.Pointer, socket C.SRTSOCKET, hsVersion C.int, peeraddr *C.struct_sockaddr, streamid *C.char) C.int {
	userCB := gopointer.Restore(arg).(ListenCallbackFunc)

	s := new(SrtSocket)
	s.socket = socket
	udpAddr, _ := udpAddrFromSockaddr((*syscall.RawSockaddrAny)(unsafe.Pointer(peeraddr)))

	if userCB(s, int(hsVersion), udpAddr, C.GoString(streamid)) {
		return 0
	}
	return SRT_ERROR
}

// SetListenCallback - set a function to be called early in the handshake before a client
// is handed to accept on a listening socket.
// The connection can be rejected by returning false from the callback.
// See examples/echo-receiver for more details.
func (s SrtSocket) SetListenCallback(cb ListenCallbackFunc) {
	ptr := gopointer.Save(cb)
	C.srt_listen_callback(s.socket, (*C.srt_listen_callback_fn)(C.srtListenCB), ptr)

	callbackMutex.Lock()
	defer callbackMutex.Unlock()
	if listenCallbackMap[s.socket] != nil {
		gopointer.Unref(listenCallbackMap[s.socket])
	}
	listenCallbackMap[s.socket] = ptr
}

// ConnectCallbackFunc specifies a function to be called after a socket or connection in a group has failed.
type ConnectCallbackFunc func(socket *SrtSocket, err error, addr *net.UDPAddr, token int)

//export srtConnectCBWrapper
func srtConnectCBWrapper(arg unsafe.Pointer, socket C.SRTSOCKET, errcode C.int, peeraddr *C.struct_sockaddr, token C.int) {
	userCB := gopointer.Restore(arg).(ConnectCallbackFunc)

	s := new(SrtSocket)
	s.socket = socket
	udpAddr, _ := udpAddrFromSockaddr((*syscall.RawSockaddrAny)(unsafe.Pointer(peeraddr)))

	userCB(s, SRTErrno(errcode), udpAddr, int(token))
}

// SetConnectCallback - set a function to be called after a socket or connection in a group has failed
// Note that the function is not guaranteed to be called if the socket is set to blocking mode.
func (s SrtSocket) SetConnectCallback(cb ConnectCallbackFunc) {
	ptr := gopointer.Save(cb)
	C.srt_connect_callback(s.socket, (*C.srt_connect_callback_fn)(C.srtConnectCB), ptr)

	callbackMutex.Lock()
	defer callbackMutex.Unlock()
	if connectCallbackMap[s.socket] != nil {
		gopointer.Unref(connectCallbackMap[s.socket])
	}
	connectCallbackMap[s.socket] = ptr
}

// Rejection reasons
var (
	// Start of range for predefined rejection reasons
	RejectionReasonPredefined = int(C.get_srt_error_reject_predefined())

	// General syntax error in the SocketID specification (also a fallback code for undefined cases)
	RejectionReasonBadRequest = RejectionReasonPredefined + 400

	// Authentication failed, provided that the user was correctly identified and access to the required resource would be granted
	RejectionReasonUnauthorized = RejectionReasonPredefined + 401

	// The server is too heavily loaded, or you have exceeded credits for accessing the service and the resource.
	RejectionReasonOverload = RejectionReasonPredefined + 402

	// Access denied to the resource by any kind of reason
	RejectionReasonForbidden = RejectionReasonPredefined + 403

	// Resource not found at this time.
	RejectionReasonNotFound = RejectionReasonPredefined + 404

	// The mode specified in `m` key in StreamID is not supported for this request.
	RejectionReasonBadMode = RejectionReasonPredefined + 405

	// The requested parameters specified in SocketID cannot be satisfied for the requested resource. Also when m=publish and the data format is not acceptable.
	RejectionReasonUnacceptable = RejectionReasonPredefined + 406

	// Start of range for application defined rejection reasons
	RejectionReasonUserDefined = int(C.get_srt_error_reject_predefined())
)

// SetRejectReason - set custom reason for connection reject
func (s SrtSocket) SetRejectReason(value int) error {
	res := C.srt_setrejectreason(s.socket, C.int(value))
	if res == SRT_ERROR {
		return errors.New(C.GoString(C.srt_getlasterror_str()))
	}
	return nil
}

// GetSockOptByte - return byte value obtained with srt_getsockopt
func (s SrtSocket) GetSockOptByte(opt int) (byte, error) {
	var v byte
	l := 1

	err := s.getSockOpt(opt, unsafe.Pointer(&v), &l)
	return v, err
}

// GetSockOptBool - return bool value obtained with srt_getsockopt
func (s SrtSocket) GetSockOptBool(opt int) (bool, error) {
	var v int32
	l := 4

	err := s.getSockOpt(opt, unsafe.Pointer(&v), &l)
	if v == 1 {
		return true, err
	}

	return false, err
}

// GetSockOptInt - return int value obtained with srt_getsockopt
func (s SrtSocket) GetSockOptInt(opt int) (int, error) {
	var v int32
	l := 4

	err := s.getSockOpt(opt, unsafe.Pointer(&v), &l)
	return int(v), err
}

// GetSockOptInt64 - return int64 value obtained with srt_getsockopt
func (s SrtSocket) GetSockOptInt64(opt int) (int64, error) {
	var v int64
	l := 8

	err := s.getSockOpt(opt, unsafe.Pointer(&v), &l)
	return v, err
}

// GetSockOptString - return string value obtained with srt_getsockopt
func (s SrtSocket) GetSockOptString(opt int) (string, error) {
	buf := make([]byte, 256)
	l := len(buf)

	err := s.getSockOpt(opt, unsafe.Pointer(&buf[0]), &l)
	if err != nil {
		return "", err
	}
	return string(buf[:l]), nil
}

// SetSockOptByte - set byte value using srt_setsockopt
func (s SrtSocket) SetSockOptByte(opt int, value byte) error {
	return s.setSockOpt(opt, unsafe.Pointer(&value), 1)
}

// SetSockOptBool - set bool value using srt_setsockopt
func (s SrtSocket) SetSockOptBool(opt int, value bool) error {
	val := int(0)
	if value {
		val = 1
	}
	return s.setSockOpt(opt, unsafe.Pointer(&val), 4)
}

// SetSockOptInt - set int value using srt_setsockopt
func (s SrtSocket) SetSockOptInt(opt int, value int) error {
	return s.setSockOpt(opt, unsafe.Pointer(&value), 4)
}

// SetSockOptInt64 - set int64 value using srt_setsockopt
func (s SrtSocket) SetSockOptInt64(opt int, value int64) error {
	return s.setSockOpt(opt, unsafe.Pointer(&value), 8)
}

// SetSockOptString - set string value using srt_setsockopt
func (s SrtSocket) SetSockOptString(opt int, value string) error {
	return s.setSockOpt(opt, unsafe.Pointer(&[]byte(value)[0]), len(value))
}

func (s SrtSocket) setSockOpt(opt int, data unsafe.Pointer, size int) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	res := C.srt_setsockopt(s.socket, 0, C.SRT_SOCKOPT(opt), data, C.int(size))
	if res == -1 {
		return fmt.Errorf("Error calling srt_setsockopt %w", srtGetAndClearError())
	}
	return nil
}

func (s SrtSocket) getSockOpt(opt int, data unsafe.Pointer, size *int) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	res := C.srt_getsockopt(s.socket, 0, C.SRT_SOCKOPT(opt), data, (*C.int)(unsafe.Pointer(size)))
	if res == -1 {
		return fmt.Errorf("Error calling srt_getsockopt %w", srtGetAndClearError())
	}

	return nil
}

func (s SrtSocket) preconfiguration() (int, error) {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	var blocking C.int
	if s.blocking {
		blocking = C.int(1)
	} else {
		blocking = C.int(0)
	}
	result := C.srt_setsockopt(s.socket, 0, C.SRTO_RCVSYN, unsafe.Pointer(&blocking), C.int(unsafe.Sizeof(blocking)))
	if result == -1 {
		return ModeFailure, fmt.Errorf("could not set SRTO_RCVSYN flag: %w", srtGetAndClearError())
	}

	var mode int
	modeVal, ok := s.options["mode"]
	if !ok {
		modeVal = "default"
	}

	if modeVal == "client" || modeVal == "caller" {
		mode = ModeCaller
	} else if modeVal == "server" || modeVal == "listener" {
		mode = ModeListener
	} else if modeVal == "default" {
		if s.host == "" {
			mode = ModeListener
		} else {
			// Host is given, so check also "adapter"
			if _, ok := s.options["adapter"]; ok {
				mode = ModeRendezvouz
			} else {
				mode = ModeCaller
			}
		}
	} else {
		mode = ModeFailure
	}

	if linger, ok := s.options["linger"]; ok {
		li, err := strconv.Atoi(linger)
		if err == nil {
			if err := setSocketLingerOption(s.socket, int32(li)); err != nil {
				return ModeFailure, fmt.Errorf("could not set LINGER option %w", err)
			}
		} else {
			return ModeFailure, fmt.Errorf("could not set LINGER option %w", err)
		}
	}

	err := setSocketOptions(s.socket, bindingPre, s.options)
	if err != nil {
		return ModeFailure, fmt.Errorf("Error setting socket options: %w", err)
	}

	return mode, nil
}

func (s SrtSocket) postconfiguration(sck *SrtSocket) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	var blocking C.int
	if s.blocking {
		blocking = 1
	} else {
		blocking = 0
	}

	res := C.srt_setsockopt(sck.socket, 0, C.SRTO_SNDSYN, unsafe.Pointer(&blocking), C.int(unsafe.Sizeof(blocking)))
	if res == -1 {
		return fmt.Errorf("Error in postconfiguration setting SRTO_SNDSYN: %w", srtGetAndClearError())
	}

	res = C.srt_setsockopt(sck.socket, 0, C.SRTO_RCVSYN, unsafe.Pointer(&blocking), C.int(unsafe.Sizeof(blocking)))
	if res == -1 {
		return fmt.Errorf("Error in postconfiguration setting SRTO_RCVSYN: %w", srtGetAndClearError())
	}

	err := setSocketOptions(sck.socket, bindingPost, s.options)
	return err
}
