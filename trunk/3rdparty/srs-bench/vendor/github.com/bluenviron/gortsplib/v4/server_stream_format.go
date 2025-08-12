package gortsplib

import (
	"sync/atomic"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpsender"
)

type serverStreamFormat struct {
	sm     *serverStreamMedia
	format format.Format

	rtcpSender     *rtcpsender.RTCPSender
	rtpPacketsSent *uint64
}

func (sf *serverStreamFormat) initialize() {
	sf.rtpPacketsSent = new(uint64)

	sf.rtcpSender = &rtcpsender.RTCPSender{
		ClockRate: sf.format.ClockRate(),
		Period:    sf.sm.st.Server.senderReportPeriod,
		TimeNow:   sf.sm.st.Server.timeNow,
		WritePacketRTCP: func(pkt rtcp.Packet) {
			if !sf.sm.st.Server.DisableRTCPSenderReports {
				sf.sm.st.WritePacketRTCP(sf.sm.media, pkt) //nolint:errcheck
			}
		},
	}
	sf.rtcpSender.Initialize()
}

func (sf *serverStreamFormat) writePacketRTP(byts []byte, pkt *rtp.Packet, ntp time.Time) error {
	sf.rtcpSender.ProcessPacket(pkt, ntp, sf.format.PTSEqualsDTS(pkt))

	le := uint64(len(byts))

	// send unicast
	for r := range sf.sm.st.activeUnicastReaders {
		if _, ok := r.setuppedMedias[sf.sm.media]; ok {
			err := r.writePacketRTP(sf.sm.media, pkt.PayloadType, byts)
			if err != nil {
				r.onStreamWriteError(err)
				continue
			}

			atomic.AddUint64(sf.sm.bytesSent, le)
			atomic.AddUint64(sf.rtpPacketsSent, 1)
		}
	}

	// send multicast
	if sf.sm.multicastWriter != nil {
		err := sf.sm.multicastWriter.writePacketRTP(byts)
		if err != nil {
			return err
		}

		atomic.AddUint64(sf.sm.bytesSent, le)
		atomic.AddUint64(sf.rtpPacketsSent, 1)
	}

	return nil
}
