package srtgo

// #cgo LDFLAGS: -lsrt
// #include <srt/srt.h>
import "C"

import (
	"errors"
	"fmt"
	"strconv"
	"syscall"
	"unsafe"
)

const (
	transTypeLive = 0
	transTypeFile = 1
)

const (
	tInteger32 = 0
	tInteger64 = 1
	tString    = 2
	tBoolean   = 3
	tTransType = 4

	SRTO_TRANSTYPE          = C.SRTO_TRANSTYPE
	SRTO_MAXBW              = C.SRTO_MAXBW
	SRTO_PBKEYLEN           = C.SRTO_PBKEYLEN
	SRTO_PASSPHRASE         = C.SRTO_PASSPHRASE
	SRTO_MSS                = C.SRTO_MSS
	SRTO_FC                 = C.SRTO_FC
	SRTO_SNDBUF             = C.SRTO_SNDBUF
	SRTO_RCVBUF             = C.SRTO_RCVBUF
	SRTO_IPTTL              = C.SRTO_IPTTL
	SRTO_IPTOS              = C.SRTO_IPTOS
	SRTO_INPUTBW            = C.SRTO_INPUTBW
	SRTO_OHEADBW            = C.SRTO_OHEADBW
	SRTO_LATENCY            = C.SRTO_LATENCY
	SRTO_TSBPDMODE          = C.SRTO_TSBPDMODE
	SRTO_TLPKTDROP          = C.SRTO_TLPKTDROP
	SRTO_SNDDROPDELAY       = C.SRTO_SNDDROPDELAY
	SRTO_NAKREPORT          = C.SRTO_NAKREPORT
	SRTO_CONNTIMEO          = C.SRTO_CONNTIMEO
	SRTO_LOSSMAXTTL         = C.SRTO_LOSSMAXTTL
	SRTO_RCVLATENCY         = C.SRTO_RCVLATENCY
	SRTO_PEERLATENCY        = C.SRTO_PEERLATENCY
	SRTO_MINVERSION         = C.SRTO_MINVERSION
	SRTO_STREAMID           = C.SRTO_STREAMID
	SRTO_CONGESTION         = C.SRTO_CONGESTION
	SRTO_MESSAGEAPI         = C.SRTO_MESSAGEAPI
	SRTO_PAYLOADSIZE        = C.SRTO_PAYLOADSIZE
	SRTO_KMREFRESHRATE      = C.SRTO_KMREFRESHRATE
	SRTO_KMPREANNOUNCE      = C.SRTO_KMPREANNOUNCE
	SRTO_ENFORCEDENCRYPTION = C.SRTO_ENFORCEDENCRYPTION
	SRTO_PEERIDLETIMEO      = C.SRTO_PEERIDLETIMEO
	SRTO_PACKETFILTER       = C.SRTO_PACKETFILTER
	SRTO_STATE              = C.SRTO_STATE
)

type socketOption struct {
	name     string
	level    int
	option   int
	binding  int
	dataType int
}

// List of possible srt socket options
var SocketOptions = []socketOption{
	{"transtype", 0, SRTO_TRANSTYPE, bindingPre, tTransType},
	{"maxbw", 0, SRTO_MAXBW, bindingPre, tInteger64},
	{"pbkeylen", 0, SRTO_PBKEYLEN, bindingPre, tInteger32},
	{"passphrase", 0, SRTO_PASSPHRASE, bindingPre, tString},
	{"mss", 0, SRTO_MSS, bindingPre, tInteger32},
	{"fc", 0, SRTO_FC, bindingPre, tInteger32},
	{"sndbuf", 0, SRTO_SNDBUF, bindingPre, tInteger32},
	{"rcvbuf", 0, SRTO_RCVBUF, bindingPre, tInteger32},
	{"ipttl", 0, SRTO_IPTTL, bindingPre, tInteger32},
	{"iptos", 0, SRTO_IPTOS, bindingPre, tInteger32},
	{"inputbw", 0, SRTO_INPUTBW, bindingPost, tInteger64},
	{"oheadbw", 0, SRTO_OHEADBW, bindingPost, tInteger32},
	{"latency", 0, SRTO_LATENCY, bindingPre, tInteger32},
	{"tsbpdmode", 0, SRTO_TSBPDMODE, bindingPre, tBoolean},
	{"tlpktdrop", 0, SRTO_TLPKTDROP, bindingPre, tBoolean},
	{"snddropdelay", 0, SRTO_SNDDROPDELAY, bindingPost, tInteger32},
	{"nakreport", 0, SRTO_NAKREPORT, bindingPre, tBoolean},
	{"conntimeo", 0, SRTO_CONNTIMEO, bindingPre, tInteger32},
	{"lossmaxttl", 0, SRTO_LOSSMAXTTL, bindingPre, tInteger32},
	{"rcvlatency", 0, SRTO_RCVLATENCY, bindingPre, tInteger32},
	{"peerlatency", 0, SRTO_PEERLATENCY, bindingPre, tInteger32},
	{"minversion", 0, SRTO_MINVERSION, bindingPre, tInteger32},
	{"streamid", 0, SRTO_STREAMID, bindingPre, tString},
	{"congestion", 0, SRTO_CONGESTION, bindingPre, tString},
	{"messageapi", 0, SRTO_MESSAGEAPI, bindingPre, tBoolean},
	{"payloadsize", 0, SRTO_PAYLOADSIZE, bindingPre, tInteger32},
	{"kmrefreshrate", 0, SRTO_KMREFRESHRATE, bindingPre, tInteger32},
	{"kmpreannounce", 0, SRTO_KMPREANNOUNCE, bindingPre, tInteger32},
	{"enforcedencryption", 0, SRTO_ENFORCEDENCRYPTION, bindingPre, tBoolean},
	{"peeridletimeo", 0, SRTO_PEERIDLETIMEO, bindingPre, tInteger32},
	{"packetfilter", 0, SRTO_PACKETFILTER, bindingPre, tString},
}

func setSocketLingerOption(s C.int, li int32) error {
	var lin syscall.Linger
	lin.Linger = li
	if lin.Linger > 0 {
		lin.Onoff = 1
	} else {
		lin.Onoff = 0
	}
	res := C.srt_setsockopt(s, bindingPre, C.SRTO_LINGER, unsafe.Pointer(&lin), C.int(unsafe.Sizeof(lin)))
	if res == SRT_ERROR {
		return errors.New("failed to set linger")
	}
	return nil
}

func getSocketLingerOption(s *SrtSocket) (int32, error) {
	var lin syscall.Linger
	size := int(unsafe.Sizeof(lin))
	err := s.getSockOpt(C.SRTO_LINGER, unsafe.Pointer(&lin), &size)
	if err != nil {
		return 0, err
	}
	if lin.Onoff == 0 {
		return 0, nil
	}
	return lin.Linger, nil
}

// Set socket options for SRT
func setSocketOptions(s C.int, binding int, options map[string]string) error {
	for _, so := range SocketOptions {
		if val, ok := options[so.name]; ok {
			if so.binding == binding {
				if so.dataType == tInteger32 {
					v, err := strconv.Atoi(val)
					v32 := int32(v)
					if err == nil {
						result := C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v32), C.int32_t(unsafe.Sizeof(v32)))
						if result == -1 {
							return fmt.Errorf("warning - error setting option %s to %s, %w", so.name, val, srtGetAndClearError())
						}
					}
				} else if so.dataType == tInteger64 {
					v, err := strconv.ParseInt(val, 10, 64)
					if err == nil {
						result := C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v), C.int32_t(unsafe.Sizeof(v)))
						if result == -1 {
							return fmt.Errorf("warning - error setting option %s to %s, %w", so.name, val, srtGetAndClearError())
						}
					}
				} else if so.dataType == tString {
					sval := C.CString(val)
					defer C.free(unsafe.Pointer(sval))
					result := C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(sval), C.int32_t(len(val)))
					if result == -1 {
						return fmt.Errorf("warning - error setting option %s to %s, %w", so.name, val, srtGetAndClearError())
					}

				} else if so.dataType == tBoolean {
					var result C.int
					if val == "1" {
						v := C.char(1)
						result = C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v), C.int32_t(unsafe.Sizeof(v)))
					} else if val == "0" {
						v := C.char(0)
						result = C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v), C.int32_t(unsafe.Sizeof(v)))
					}
					if result == -1 {
						return fmt.Errorf("warning - error setting option %s to %s, %w", so.name, val, srtGetAndClearError())
					}
				} else if so.dataType == tTransType {
					var result C.int
					if val == "live" {
						var v int32 = C.SRTT_LIVE
						result = C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v), C.int32_t(unsafe.Sizeof(v)))
					} else if val == "file" {
						var v int32 = C.SRTT_FILE
						result = C.srt_setsockflag(s, C.SRT_SOCKOPT(so.option), unsafe.Pointer(&v), C.int32_t(unsafe.Sizeof(v)))
					}
					if result == -1 {
						return fmt.Errorf("warning - error setting option %s to %s: %w", so.name, val, srtGetAndClearError())
					}
				}
			}
		}
	}
	return nil
}
