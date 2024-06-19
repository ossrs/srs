package srtgo

/*
#cgo LDFLAGS: -lsrt
#include <srt/srt.h>
extern void srtLogCB(void* opaque, int level, const char* file, int line, const char* area, const char* message);
*/
import "C"

import (
	"sync"
	"unsafe"

	gopointer "github.com/mattn/go-pointer"
)

type LogCallBackFunc func(level SrtLogLevel, file string, line int, area, message string)

type SrtLogLevel int

const (
	//	SrtLogLevelEmerg = int(C.LOG_EMERG)
	//	SrtLogLevelAlert = int(C.LOG_ALERT)
	SrtLogLevelCrit    SrtLogLevel = SrtLogLevel(C.LOG_CRIT)
	SrtLogLevelErr     SrtLogLevel = SrtLogLevel(C.LOG_ERR)
	SrtLogLevelWarning SrtLogLevel = SrtLogLevel(C.LOG_WARNING)
	SrtLogLevelNotice  SrtLogLevel = SrtLogLevel(C.LOG_NOTICE)
	SrtLogLevelInfo    SrtLogLevel = SrtLogLevel(C.LOG_INFO)
	SrtLogLevelDebug   SrtLogLevel = SrtLogLevel(C.LOG_DEBUG)
	SrtLogLevelTrace   SrtLogLevel = SrtLogLevel(8)
)

var (
	logCBPtr     unsafe.Pointer = nil
	logCBPtrLock sync.Mutex
)

//export srtLogCBWrapper
func srtLogCBWrapper(arg unsafe.Pointer, level C.int, file *C.char, line C.int, area, message *C.char) {
	userCB := gopointer.Restore(arg).(LogCallBackFunc)
	go userCB(SrtLogLevel(level), C.GoString(file), int(line), C.GoString(area), C.GoString(message))
}

func SrtSetLogLevel(level SrtLogLevel) {
	C.srt_setloglevel(C.int(level))
}

func SrtSetLogHandler(cb LogCallBackFunc) {
	ptr := gopointer.Save(cb)
	C.srt_setloghandler(ptr, (*C.SRT_LOG_HANDLER_FN)(C.srtLogCB))
	storeLogCBPtr(ptr)
}

func SrtUnsetLogHandler() {
	C.srt_setloghandler(nil, nil)
	storeLogCBPtr(nil)
}

func storeLogCBPtr(ptr unsafe.Pointer) {
	logCBPtrLock.Lock()
	defer logCBPtrLock.Unlock()
	if logCBPtr != nil {
		gopointer.Unref(logCBPtr)
	}
	logCBPtr = ptr
}
