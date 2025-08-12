package gortsplib

import (
	"sync/atomic"

	"github.com/bluenviron/gortsplib/v4/pkg/description"
)

type serverStreamMedia struct {
	st      *ServerStream
	media   *description.Media
	trackID int

	formats         map[uint8]*serverStreamFormat
	multicastWriter *serverMulticastWriter
	bytesSent       *uint64
	rtcpPacketsSent *uint64
}

func (sm *serverStreamMedia) initialize() {
	sm.bytesSent = new(uint64)
	sm.rtcpPacketsSent = new(uint64)

	sm.formats = make(map[uint8]*serverStreamFormat)
	for _, forma := range sm.media.Formats {
		sf := &serverStreamFormat{
			sm:     sm,
			format: forma,
		}
		sf.initialize()
		sm.formats[forma.PayloadType()] = sf
	}
}

func (sm *serverStreamMedia) close() {
	for _, tr := range sm.formats {
		if tr.rtcpSender != nil {
			tr.rtcpSender.Close()
		}
	}

	if sm.multicastWriter != nil {
		sm.multicastWriter.close()
	}
}

func (sm *serverStreamMedia) writePacketRTCP(byts []byte) error {
	le := len(byts)

	// send unicast
	for r := range sm.st.activeUnicastReaders {
		if _, ok := r.setuppedMedias[sm.media]; ok {
			err := r.writePacketRTCP(sm.media, byts)
			if err != nil {
				r.onStreamWriteError(err)
				continue
			}

			atomic.AddUint64(sm.bytesSent, uint64(le))
			atomic.AddUint64(sm.rtcpPacketsSent, 1)
		}
	}

	// send multicast
	if sm.multicastWriter != nil {
		err := sm.multicastWriter.writePacketRTCP(byts)
		if err != nil {
			return err
		}

		atomic.AddUint64(sm.bytesSent, uint64(le))
		atomic.AddUint64(sm.rtcpPacketsSent, 1)
	}

	return nil
}
