// Copyright (c) 2026 Winlin
//
// SPDX-License-Identifier: MIT

package main

/*
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <syslog.h>
#include <srt/srt.h>

static int srtl_addr(const char* ip, int port, struct sockaddr_storage* ss, int* len) {
	memset(ss, 0, sizeof(*ss));
	struct sockaddr_in* a4 = (struct sockaddr_in*)ss;
	if (inet_pton(AF_INET, ip, &a4->sin_addr) == 1) {
		a4->sin_family = AF_INET;
		a4->sin_port = htons(port);
		*len = sizeof(*a4);
		return 0;
	}
	struct sockaddr_in6* a6 = (struct sockaddr_in6*)ss;
	if (inet_pton(AF_INET6, ip, &a6->sin6_addr) == 1) {
		a6->sin6_family = AF_INET6;
		a6->sin6_port = htons(port);
		*len = sizeof(*a6);
		return 0;
	}
	return -1;
}

static int srtl_text(const struct sockaddr_storage* ss, char* ip, int n, int* port) {
	if (ss->ss_family == AF_INET) {
		const struct sockaddr_in* a4 = (const struct sockaddr_in*)ss;
		*port = ntohs(a4->sin_port);
		return inet_ntop(AF_INET, &a4->sin_addr, ip, n) ? 0 : -1;
	}
	if (ss->ss_family == AF_INET6) {
		const struct sockaddr_in6* a6 = (const struct sockaddr_in6*)ss;
		*port = ntohs(a6->sin6_port);
		return inet_ntop(AF_INET6, &a6->sin6_addr, ip, n) ? 0 : -1;
	}
	return -1;
}

static int srtl_connect(SRTSOCKET s, const char* ip, int port) {
	struct sockaddr_storage ss;
	int len;
	if (srtl_addr(ip, port, &ss, &len) != 0) {
		return -2;
	}
	return srt_connect(s, (struct sockaddr*)&ss, len);
}

static int srtl_bind(SRTSOCKET s, const char* ip, int port) {
	struct sockaddr_storage ss;
	int len;
	if (srtl_addr(ip, port, &ss, &len) != 0) {
		return -2;
	}
	return srt_bind(s, (struct sockaddr*)&ss, len);
}

static int srtl_sockname(SRTSOCKET s, int peer, char* ip, int n, int* port) {
	struct sockaddr_storage ss;
	int len = sizeof(ss);
	int r = peer ? srt_getpeername(s, (struct sockaddr*)&ss, &len) : srt_getsockname(s, (struct sockaddr*)&ss, &len);
	if (r != 0) {
		return r;
	}
	return srtl_text(&ss, ip, n, port);
}

static SRTSOCKET srtl_accept(SRTSOCKET s, char* ip, int n, int* port) {
	struct sockaddr_storage ss;
	int len = sizeof(ss);
	SRTSOCKET c = srt_accept(s, (struct sockaddr*)&ss, &len);
	if (c != SRT_INVALID_SOCK) {
		srtl_text(&ss, ip, n, port);
	}
	return c;
}

static int srtl_setint(SRTSOCKET s, SRT_SOCKOPT o, int v) { return srt_setsockflag(s, o, &v, sizeof(v)); }
static int srtl_setint64(SRTSOCKET s, SRT_SOCKOPT o, int64_t v) { return srt_setsockflag(s, o, &v, sizeof(v)); }
static int srtl_setstr(SRTSOCKET s, SRT_SOCKOPT o, const char* v) { return srt_setsockflag(s, o, v, strlen(v)); }

static int srtl_getstr(SRTSOCKET s, SRT_SOCKOPT o, char* v, int n) {
	int len = n;
	int r = srt_getsockflag(s, o, v, &len);
	if (r == 0) {
		v[len < n ? len : n - 1] = 0;
	}
	return r;
}

static int srtl_sndbuffer(SRTSOCKET s, int64_t* bytes) {
	size_t blocks = 0, b = 0;
	int r = srt_getsndbuffer(s, &blocks, &b);
	*bytes = (int64_t)b;
	return r;
}
*/
import "C"

import (
	"fmt"
	"net"
	"runtime"
	"unsafe"
)

func init() {
	C.srt_startup()
	// libsrt warns of each rejected handshake and more; the events say it.
	C.srt_setloglevel(C.LOG_ERR)
}

// A socket is one libsrt socket.
type socket struct{ s C.SRTSOCKET }

// An srtError is libsrt's error of a call, its code and message.
type srtError struct {
	op   string
	code int
	msg  string
}

func (e *srtError) Error() string { return fmt.Sprintf("%v: %v", e.op, e.msg) }

// A rejectError is a handshake the peer refused, with the reason as libsrt
// numbers it on the wire: 1000 and more.
type rejectError struct {
	reason int
	msg    string
}

func (e *rejectError) Error() string {
	return fmt.Sprintf("connect: rejected, reason %v: %v", e.reason, e.msg)
}

// call runs f, and returns libsrt's error when f gives SRT_ERROR. libsrt keeps
// the last error per thread, so f and the error are read on one thread.
func call(op string, f func() C.int) error {
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	if r := f(); r >= 0 {
		return nil
	}
	return lastError(op)
}

func lastError(op string) error {
	code := int(C.srt_getlasterror(nil))
	msg := C.GoString(C.srt_getlasterror_str())
	C.srt_clearlasterror()
	return &srtError{op: op, code: code, msg: msg}
}

func newSocket() (*socket, error) {
	var s C.SRTSOCKET
	err := call("socket", func() C.int {
		s = C.srt_create_socket()
		if s < 0 {
			return -1
		}
		return 0
	})
	if err != nil {
		return nil, err
	}
	return &socket{s: s}, nil
}

// An option is a libsrt socket option.
type option struct {
	name string
	opt  C.SRT_SOCKOPT
}

func (s *socket) setInt(o option, v int) error {
	return wrapOption(o, call("setsockflag", func() C.int { return C.srtl_setint(s.s, o.opt, C.int(v)) }))
}

func (s *socket) setInt64(o option, v int64) error {
	return wrapOption(o, call("setsockflag", func() C.int { return C.srtl_setint64(s.s, o.opt, C.int64_t(v)) }))
}

func (s *socket) setString(o option, v string) error {
	cs := C.CString(v)
	defer C.free(unsafe.Pointer(cs))
	return wrapOption(o, call("setsockflag", func() C.int { return C.srtl_setstr(s.s, o.opt, cs) }))
}

func wrapOption(o option, err error) error {
	if err != nil {
		return fmt.Errorf("option %v: %w", o.name, err)
	}
	return nil
}

func (s *socket) getString(o option) (string, error) {
	buf := make([]byte, 1024)
	p := (*C.char)(unsafe.Pointer(&buf[0]))
	if err := call("getsockflag", func() C.int { return C.srtl_getstr(s.s, o.opt, p, C.int(len(buf))) }); err != nil {
		return "", wrapOption(o, err)
	}
	return C.GoString(p), nil
}

// connect calls addr; a refused handshake gives a *rejectError.
func (s *socket) connect(addr *net.UDPAddr) error {
	ip := C.CString(addr.IP.String())
	defer C.free(unsafe.Pointer(ip))
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	r := C.srtl_connect(s.s, ip, C.int(addr.Port))
	if r == -2 {
		return fmt.Errorf("connect: bad address %v", addr)
	}
	if r >= 0 {
		return nil
	}
	err := lastError("connect")
	if e, ok := err.(*srtError); ok && e.code == int(C.SRT_ECONNREJ) {
		return &rejectError{reason: 1000 + int(C.srt_getrejectreason(s.s)), msg: e.msg}
	}
	return err
}

func (s *socket) bind(addr *net.UDPAddr) error {
	ip := C.CString(addr.IP.String())
	defer C.free(unsafe.Pointer(ip))
	runtime.LockOSThread()
	defer runtime.UnlockOSThread()
	r := C.srtl_bind(s.s, ip, C.int(addr.Port))
	if r == -2 {
		return fmt.Errorf("bind: bad address %v", addr)
	}
	if r < 0 {
		return lastError("bind")
	}
	return nil
}

func (s *socket) listen(backlog int) error {
	return call("listen", func() C.int { return C.srt_listen(s.s, C.int(backlog)) })
}

// accept waits for a caller; closing s ends the wait with an error.
func (s *socket) accept() (*socket, *net.UDPAddr, error) {
	buf := make([]byte, 64)
	p := (*C.char)(unsafe.Pointer(&buf[0]))
	var port C.int
	var c C.SRTSOCKET
	err := call("accept", func() C.int {
		c = C.srtl_accept(s.s, p, C.int(len(buf)), &port)
		if c < 0 {
			return -1
		}
		return 0
	})
	if err != nil {
		return nil, nil, err
	}
	return &socket{s: c}, &net.UDPAddr{IP: net.ParseIP(C.GoString(p)), Port: int(port)}, nil
}

// name returns the local address, or the peer's when peer is set.
func (s *socket) name(peer bool) (*net.UDPAddr, error) {
	buf := make([]byte, 64)
	p := (*C.char)(unsafe.Pointer(&buf[0]))
	var port C.int
	isPeer := C.int(0)
	if peer {
		isPeer = 1
	}
	if err := call("sockname", func() C.int { return C.srtl_sockname(s.s, isPeer, p, C.int(len(buf)), &port) }); err != nil {
		return nil, err
	}
	return &net.UDPAddr{IP: net.ParseIP(C.GoString(p)), Port: int(port)}, nil
}

// recv reads one message; it fails when the connection ends.
func (s *socket) recv(p []byte) (int, error) {
	var n C.int
	err := call("recvmsg", func() C.int {
		n = C.srt_recvmsg(s.s, (*C.char)(unsafe.Pointer(&p[0])), C.int(len(p)))
		return n
	})
	return int(n), err
}

// send sends p as one message.
func (s *socket) send(p []byte) error {
	return call("sendmsg", func() C.int {
		return C.srt_sendmsg2(s.s, (*C.char)(unsafe.Pointer(&p[0])), C.int(len(p)), nil)
	})
}

// unacked is the number of bytes sent but not yet acknowledged.
func (s *socket) unacked() (int64, error) {
	var b C.int64_t
	err := call("getsndbuffer", func() C.int { return C.srtl_sndbuffer(s.s, &b) })
	return int64(b), err
}

func (s *socket) connected() bool {
	return C.srt_getsockstate(s.s) == C.SRTS_CONNECTED
}

// Stats are libsrt's totals of a connection, by its names, the delays and the
// MSS the handshake agreed, and the flow window: how many packets the peer
// takes in flight, the free space of its receiver buffer.
type Stats struct {
	PktSent         int64   `json:"pktSent"`
	PktRecv         int64   `json:"pktRecv"`
	PktSentUnique   int64   `json:"pktSentUnique"`
	PktRecvUnique   int64   `json:"pktRecvUnique"`
	PktSndLoss      int     `json:"pktSndLoss"`
	PktRcvLoss      int     `json:"pktRcvLoss"`
	PktRetrans      int     `json:"pktRetrans"`
	PktRcvRetrans   int     `json:"pktRcvRetrans"`
	PktSndDrop      int     `json:"pktSndDrop"`
	PktRcvDrop      int     `json:"pktRcvDrop"`
	PktRcvUndecrypt int     `json:"pktRcvUndecrypt"`
	ByteSent        uint64  `json:"byteSent"`
	ByteRecv        uint64  `json:"byteRecv"`
	MsRTT           float64 `json:"msRTT"`
	MbpsSendRate    float64 `json:"mbpsSendRate"`
	MbpsRecvRate    float64 `json:"mbpsRecvRate"`
	MsRcvTsbPdDelay int     `json:"msRcvTsbPdDelay"`
	MsSndTsbPdDelay int     `json:"msSndTsbPdDelay"`
	ByteMSS         int     `json:"byteMSS"`
	PktFlowWindow   int     `json:"pktFlowWindow"`
}

func (s *socket) stats() (Stats, error) {
	var p C.SRT_TRACEBSTATS
	if err := call("bstats", func() C.int { return C.srt_bstats(s.s, &p, 0) }); err != nil {
		return Stats{}, err
	}
	return Stats{
		PktSent: int64(p.pktSentTotal), PktRecv: int64(p.pktRecvTotal),
		PktSentUnique: int64(p.pktSentUniqueTotal), PktRecvUnique: int64(p.pktRecvUniqueTotal),
		PktSndLoss: int(p.pktSndLossTotal), PktRcvLoss: int(p.pktRcvLossTotal),
		PktRetrans: int(p.pktRetransTotal), PktRcvRetrans: int(p.pktRcvRetrans),
		PktSndDrop: int(p.pktSndDropTotal), PktRcvDrop: int(p.pktRcvDropTotal),
		PktRcvUndecrypt: int(p.pktRcvUndecryptTotal),
		ByteSent:        uint64(p.byteSentTotal), ByteRecv: uint64(p.byteRecvTotal),
		MsRTT: float64(p.msRTT), MbpsSendRate: float64(p.mbpsSendRate), MbpsRecvRate: float64(p.mbpsRecvRate),
		MsRcvTsbPdDelay: int(p.msRcvTsbPdDelay), MsSndTsbPdDelay: int(p.msSndTsbPdDelay),
		ByteMSS: int(p.byteMSS), PktFlowWindow: int(p.pktFlowWindow),
	}, nil
}

func (s *socket) close() {
	C.srt_close(s.s)
}
