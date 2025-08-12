package gortsplib

import (
	"log"
	"sync/atomic"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/format"
	"github.com/bluenviron/gortsplib/v4/pkg/liberrors"
	"github.com/bluenviron/gortsplib/v4/pkg/rtcpreceiver"
	"github.com/bluenviron/gortsplib/v4/pkg/rtplossdetector"
	"github.com/bluenviron/gortsplib/v4/pkg/rtpreorderer"
)

type serverSessionFormat struct {
	sm          *serverSessionMedia
	format      format.Format
	onPacketRTP OnPacketRTPFunc

	udpReorderer          *rtpreorderer.Reorderer
	tcpLossDetector       *rtplossdetector.LossDetector
	rtcpReceiver          *rtcpreceiver.RTCPReceiver
	writePacketRTPInQueue func([]byte) error
	rtpPacketsReceived    *uint64
	rtpPacketsSent        *uint64
	rtpPacketsLost        *uint64
}

func (sf *serverSessionFormat) initialize() {
	sf.rtpPacketsReceived = new(uint64)
	sf.rtpPacketsSent = new(uint64)
	sf.rtpPacketsLost = new(uint64)
}

func (sf *serverSessionFormat) start() {
	switch *sf.sm.ss.setuppedTransport {
	case TransportUDP, TransportUDPMulticast:
		sf.writePacketRTPInQueue = sf.writePacketRTPInQueueUDP

	default:
		sf.writePacketRTPInQueue = sf.writePacketRTPInQueueTCP
	}

	if sf.sm.ss.state != ServerSessionStatePlay {
		if *sf.sm.ss.setuppedTransport == TransportUDP || *sf.sm.ss.setuppedTransport == TransportUDPMulticast {
			sf.udpReorderer = &rtpreorderer.Reorderer{}
			sf.udpReorderer.Initialize()
		} else {
			sf.tcpLossDetector = &rtplossdetector.LossDetector{}
		}

		sf.rtcpReceiver = &rtcpreceiver.RTCPReceiver{
			ClockRate: sf.format.ClockRate(),
			Period:    sf.sm.ss.s.receiverReportPeriod,
			TimeNow:   sf.sm.ss.s.timeNow,
			WritePacketRTCP: func(pkt rtcp.Packet) {
				if *sf.sm.ss.setuppedTransport == TransportUDP || *sf.sm.ss.setuppedTransport == TransportUDPMulticast {
					sf.sm.ss.WritePacketRTCP(sf.sm.media, pkt) //nolint:errcheck
				}
			},
		}
		err := sf.rtcpReceiver.Initialize()
		if err != nil {
			panic(err)
		}
	}
}

func (sf *serverSessionFormat) stop() {
	if sf.rtcpReceiver != nil {
		sf.rtcpReceiver.Close()
		sf.rtcpReceiver = nil
	}
}

func (sf *serverSessionFormat) readPacketRTPUDP(pkt *rtp.Packet, now time.Time) {
	packets, lost := sf.udpReorderer.Process(pkt)
	if lost != 0 {
		sf.onPacketRTPLost(uint64(lost))
		// do not return
	}

	for _, pkt := range packets {
		sf.handlePacketRTP(pkt, now)
	}
}

func (sf *serverSessionFormat) readPacketRTPTCP(pkt *rtp.Packet) {
	lost := sf.tcpLossDetector.Process(pkt)
	if lost != 0 {
		sf.onPacketRTPLost(uint64(lost))
		// do not return
	}

	now := sf.sm.ss.s.timeNow()

	sf.handlePacketRTP(pkt, now)
}

func (sf *serverSessionFormat) handlePacketRTP(pkt *rtp.Packet, now time.Time) {
	err := sf.rtcpReceiver.ProcessPacket(pkt, now, sf.format.PTSEqualsDTS(pkt))
	if err != nil {
		sf.sm.onPacketRTPDecodeError(err)
		return
	}

	atomic.AddUint64(sf.rtpPacketsReceived, 1)

	sf.onPacketRTP(pkt)
}

func (sf *serverSessionFormat) onPacketRTPLost(lost uint64) {
	atomic.AddUint64(sf.rtpPacketsLost, lost)

	if h, ok := sf.sm.ss.s.Handler.(ServerHandlerOnPacketsLost); ok {
		h.OnPacketsLost(&ServerHandlerOnPacketsLostCtx{
			Session: sf.sm.ss,
			Lost:    lost,
		})
	} else if h, ok := sf.sm.ss.s.Handler.(ServerHandlerOnPacketLost); ok {
		h.OnPacketLost(&ServerHandlerOnPacketLostCtx{
			Session: sf.sm.ss,
			Error:   liberrors.ErrServerRTPPacketsLost{Lost: uint(lost)}, //nolint:staticcheck
		})
	} else {
		log.Printf("%d RTP %s lost",
			lost,
			func() string {
				if lost == 1 {
					return "packet"
				}
				return "packets"
			}())
	}
}

func (sf *serverSessionFormat) writePacketRTPInQueueUDP(payload []byte) error {
	err := sf.sm.ss.s.udpRTPListener.write(payload, sf.sm.udpRTPWriteAddr)
	if err != nil {
		return err
	}

	atomic.AddUint64(sf.sm.bytesSent, uint64(len(payload)))
	atomic.AddUint64(sf.rtpPacketsSent, 1)
	return nil
}

func (sf *serverSessionFormat) writePacketRTPInQueueTCP(payload []byte) error {
	sf.sm.ss.tcpFrame.Channel = sf.sm.tcpChannel
	sf.sm.ss.tcpFrame.Payload = payload
	sf.sm.ss.tcpConn.nconn.SetWriteDeadline(time.Now().Add(sf.sm.ss.s.WriteTimeout))
	err := sf.sm.ss.tcpConn.conn.WriteInterleavedFrame(sf.sm.ss.tcpFrame, sf.sm.ss.tcpBuffer)
	if err != nil {
		return err
	}

	atomic.AddUint64(sf.sm.bytesSent, uint64(len(payload)))
	atomic.AddUint64(sf.rtpPacketsSent, 1)
	return nil
}
