package gortsplib

import (
	"net"
	"sync/atomic"
	"time"

	"github.com/pion/rtcp"
	"github.com/pion/rtp"

	"github.com/bluenviron/gortsplib/v4/pkg/description"
	"github.com/bluenviron/gortsplib/v4/pkg/liberrors"
)

type clientMedia struct {
	c     *Client
	media *description.Media

	onPacketRTCP           OnPacketRTCPFunc
	formats                map[uint8]*clientFormat
	tcpChannel             int
	udpRTPListener         *clientUDPListener
	udpRTCPListener        *clientUDPListener
	writePacketRTCPInQueue func([]byte) error
	bytesReceived          *uint64
	bytesSent              *uint64
	rtpPacketsInError      *uint64
	rtcpPacketsReceived    *uint64
	rtcpPacketsSent        *uint64
	rtcpPacketsInError     *uint64
}

func (cm *clientMedia) initialize() {
	cm.onPacketRTCP = func(rtcp.Packet) {}
	cm.bytesReceived = new(uint64)
	cm.bytesSent = new(uint64)
	cm.rtpPacketsInError = new(uint64)
	cm.rtcpPacketsReceived = new(uint64)
	cm.rtcpPacketsSent = new(uint64)
	cm.rtcpPacketsInError = new(uint64)

	cm.formats = make(map[uint8]*clientFormat)

	for _, forma := range cm.media.Formats {
		f := &clientFormat{
			cm:          cm,
			format:      forma,
			onPacketRTP: func(*rtp.Packet) {},
		}
		f.initialize()
		cm.formats[forma.PayloadType()] = f
	}
}

func (cm *clientMedia) close() {
	if cm.udpRTPListener != nil {
		cm.udpRTPListener.close()
		cm.udpRTCPListener.close()
	}
}

func (cm *clientMedia) createUDPListeners(
	multicastEnable bool,
	multicastSourceIP net.IP,
	rtpAddress string,
	rtcpAddress string,
) error {
	if rtpAddress != ":0" {
		l1 := &clientUDPListener{
			c:                 cm.c,
			multicastEnable:   multicastEnable,
			multicastSourceIP: multicastSourceIP,
			address:           rtpAddress,
		}
		err := l1.initialize()
		if err != nil {
			return err
		}

		l2 := &clientUDPListener{
			c:                 cm.c,
			multicastEnable:   multicastEnable,
			multicastSourceIP: multicastSourceIP,
			address:           rtcpAddress,
		}
		err = l2.initialize()
		if err != nil {
			l1.close()
			return err
		}

		cm.udpRTPListener, cm.udpRTCPListener = l1, l2
		return nil
	}

	var err error
	cm.udpRTPListener, cm.udpRTCPListener, err = createUDPListenerPair(cm.c)
	return err
}

func (cm *clientMedia) start() {
	if cm.udpRTPListener != nil {
		cm.writePacketRTCPInQueue = cm.writePacketRTCPInQueueUDP

		if cm.c.state == clientStateRecord || cm.media.IsBackChannel {
			cm.udpRTPListener.readFunc = cm.readPacketRTPUDPRecord
			cm.udpRTCPListener.readFunc = cm.readPacketRTCPUDPRecord
		} else {
			cm.udpRTPListener.readFunc = cm.readPacketRTPUDPPlay
			cm.udpRTCPListener.readFunc = cm.readPacketRTCPUDPPlay
		}
	} else {
		cm.writePacketRTCPInQueue = cm.writePacketRTCPInQueueTCP

		if cm.c.tcpCallbackByChannel == nil {
			cm.c.tcpCallbackByChannel = make(map[int]readFunc)
		}

		if cm.c.state == clientStateRecord || cm.media.IsBackChannel {
			cm.c.tcpCallbackByChannel[cm.tcpChannel] = cm.readPacketRTPTCPRecord
			cm.c.tcpCallbackByChannel[cm.tcpChannel+1] = cm.readPacketRTCPTCPRecord
		} else {
			cm.c.tcpCallbackByChannel[cm.tcpChannel] = cm.readPacketRTPTCPPlay
			cm.c.tcpCallbackByChannel[cm.tcpChannel+1] = cm.readPacketRTCPTCPPlay
		}
	}

	for _, ct := range cm.formats {
		ct.start()
	}

	if cm.udpRTPListener != nil {
		cm.udpRTPListener.start()
		cm.udpRTCPListener.start()
	}
}

func (cm *clientMedia) stop() {
	if cm.udpRTPListener != nil {
		cm.udpRTPListener.stop()
		cm.udpRTCPListener.stop()
	}

	for _, ct := range cm.formats {
		ct.stop()
	}
}

func (cm *clientMedia) findFormatWithSSRC(ssrc uint32) *clientFormat {
	for _, format := range cm.formats {
		stats := format.rtcpReceiver.Stats()
		if stats != nil && stats.RemoteSSRC == ssrc {
			return format
		}
	}
	return nil
}

func (cm *clientMedia) writePacketRTCPInQueueUDP(payload []byte) error {
	err := cm.udpRTCPListener.write(payload)
	if err != nil {
		return err
	}

	atomic.AddUint64(cm.bytesSent, uint64(len(payload)))
	atomic.AddUint64(cm.rtcpPacketsSent, 1)
	return nil
}

func (cm *clientMedia) writePacketRTCPInQueueTCP(payload []byte) error {
	cm.c.tcpFrame.Channel = cm.tcpChannel + 1
	cm.c.tcpFrame.Payload = payload
	cm.c.nconn.SetWriteDeadline(time.Now().Add(cm.c.WriteTimeout))
	err := cm.c.conn.WriteInterleavedFrame(cm.c.tcpFrame, cm.c.tcpBuffer)
	if err != nil {
		return err
	}

	atomic.AddUint64(cm.bytesSent, uint64(len(payload)))
	atomic.AddUint64(cm.rtcpPacketsSent, 1)
	return nil
}

func (cm *clientMedia) readPacketRTPTCPPlay(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	now := cm.c.timeNow()
	atomic.StoreInt64(cm.c.tcpLastFrameTime, now.Unix())

	pkt := &rtp.Packet{}
	err := pkt.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTPDecodeError(err)
		return false
	}

	forma, ok := cm.formats[pkt.PayloadType]
	if !ok {
		cm.onPacketRTPDecodeError(liberrors.ErrClientRTPPacketUnknownPayloadType{PayloadType: pkt.PayloadType})
		return false
	}

	forma.readPacketRTPTCP(pkt)

	return true
}

func (cm *clientMedia) readPacketRTCPTCPPlay(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	now := cm.c.timeNow()
	atomic.StoreInt64(cm.c.tcpLastFrameTime, now.Unix())

	if len(payload) > udpMaxPayloadSize {
		cm.onPacketRTCPDecodeError(liberrors.ErrClientRTCPPacketTooBig{L: len(payload), Max: udpMaxPayloadSize})
		return false
	}

	packets, err := rtcp.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTCPDecodeError(err)
		return false
	}

	atomic.AddUint64(cm.rtcpPacketsReceived, uint64(len(packets)))

	for _, pkt := range packets {
		if sr, ok := pkt.(*rtcp.SenderReport); ok {
			format := cm.findFormatWithSSRC(sr.SSRC)
			if format != nil {
				format.rtcpReceiver.ProcessSenderReport(sr, now)
			}
		}

		cm.onPacketRTCP(pkt)
	}

	return true
}

func (cm *clientMedia) readPacketRTPTCPRecord(_ []byte) bool {
	return false
}

func (cm *clientMedia) readPacketRTCPTCPRecord(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	if len(payload) > udpMaxPayloadSize {
		cm.onPacketRTCPDecodeError(liberrors.ErrClientRTCPPacketTooBig{L: len(payload), Max: udpMaxPayloadSize})
		return false
	}

	packets, err := rtcp.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTCPDecodeError(err)
		return false
	}

	atomic.AddUint64(cm.rtcpPacketsReceived, uint64(len(packets)))

	for _, pkt := range packets {
		cm.onPacketRTCP(pkt)
	}

	return true
}

func (cm *clientMedia) readPacketRTPUDPPlay(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	if len(payload) == (udpMaxPayloadSize + 1) {
		cm.onPacketRTPDecodeError(liberrors.ErrClientRTPPacketTooBigUDP{})
		return false
	}

	pkt := &rtp.Packet{}
	err := pkt.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTPDecodeError(err)
		return false
	}

	forma, ok := cm.formats[pkt.PayloadType]
	if !ok {
		cm.onPacketRTPDecodeError(liberrors.ErrClientRTPPacketUnknownPayloadType{PayloadType: pkt.PayloadType})
		return false
	}

	forma.readPacketRTPUDP(pkt)

	return true
}

func (cm *clientMedia) readPacketRTCPUDPPlay(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	if len(payload) == (udpMaxPayloadSize + 1) {
		cm.onPacketRTCPDecodeError(liberrors.ErrClientRTCPPacketTooBigUDP{})
		return false
	}

	packets, err := rtcp.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTCPDecodeError(err)
		return false
	}

	now := cm.c.timeNow()

	atomic.AddUint64(cm.rtcpPacketsReceived, uint64(len(packets)))

	for _, pkt := range packets {
		if sr, ok := pkt.(*rtcp.SenderReport); ok {
			format := cm.findFormatWithSSRC(sr.SSRC)
			if format != nil {
				format.rtcpReceiver.ProcessSenderReport(sr, now)
			}
		}

		cm.onPacketRTCP(pkt)
	}

	return true
}

func (cm *clientMedia) readPacketRTPUDPRecord(_ []byte) bool {
	return false
}

func (cm *clientMedia) readPacketRTCPUDPRecord(payload []byte) bool {
	atomic.AddUint64(cm.bytesReceived, uint64(len(payload)))

	if len(payload) == (udpMaxPayloadSize + 1) {
		cm.onPacketRTCPDecodeError(liberrors.ErrClientRTCPPacketTooBigUDP{})
		return false
	}

	packets, err := rtcp.Unmarshal(payload)
	if err != nil {
		cm.onPacketRTCPDecodeError(err)
		return false
	}

	atomic.AddUint64(cm.rtcpPacketsReceived, uint64(len(packets)))

	for _, pkt := range packets {
		cm.onPacketRTCP(pkt)
	}

	return true
}

func (cm *clientMedia) onPacketRTPDecodeError(err error) {
	atomic.AddUint64(cm.rtpPacketsInError, 1)
	cm.c.OnDecodeError(err)
}

func (cm *clientMedia) onPacketRTCPDecodeError(err error) {
	atomic.AddUint64(cm.rtcpPacketsInError, 1)
	cm.c.OnDecodeError(err)
}
