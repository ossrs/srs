package srtgo

/*
#cgo LDFLAGS: -lsrt
#include <srt/srt.h>

int srt_sendmsg2_wrapped(SRTSOCKET u, const char* buf, int len, SRT_MSGCTRL *mctrl, int *srterror, int *syserror)
{
	int ret = srt_sendmsg2(u, buf, len, mctrl);
	if (ret < 0) {
		*srterror = srt_getlasterror(syserror);
	}
	return ret;
}

*/
import "C"
import (
	"errors"
	"syscall"
	"unsafe"
)

func srtSendMsg2Impl(u C.SRTSOCKET, buf []byte, msgctrl *C.SRT_MSGCTRL) (n int, err error) {
	srterr := C.int(0)
	syserr := C.int(0)
	n = int(C.srt_sendmsg2_wrapped(u, (*C.char)(unsafe.Pointer(&buf[0])), C.int(len(buf)), msgctrl, &srterr, &syserr))
	if n < 0 {
		srterror := SRTErrno(srterr)
		if syserr < 0 {
			srterror.wrapSysErr(syscall.Errno(syserr))
		}
		err = srterror
		n = 0
	}
	return
}

// Write data to the SRT socket
func (s SrtSocket) Write(b []byte) (n int, err error) {

	//Fastpath:
	if !s.blocking {
		s.pd.reset(ModeWrite)
	}
	n, err = srtSendMsg2Impl(s.socket, b, nil)

	for {
		if !errors.Is(err, error(EAsyncSND)) || s.blocking {
			return
		}
		s.pd.wait(ModeWrite)
		n, err = srtSendMsg2Impl(s.socket, b, nil)
	}
}
