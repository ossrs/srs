// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package nack

import (
	"sync"

	"github.com/pion/interceptor"
	"github.com/pion/logging"
	"github.com/pion/rtcp"
	"github.com/pion/rtp"
)

// ResponderInterceptorFactory is a interceptor.Factory for a ResponderInterceptor
type ResponderInterceptorFactory struct {
	opts []ResponderOption
}

type packetFactory interface {
	NewPacket(header *rtp.Header, payload []byte, rtxSsrc uint32, rtxPayloadType uint8) (*retainablePacket, error)
}

// NewInterceptor constructs a new ResponderInterceptor
func (r *ResponderInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	i := &ResponderInterceptor{
		streamsFilter: streamSupportNack,
		size:          1024,
		log:           logging.NewDefaultLoggerFactory().NewLogger("nack_responder"),
		streams:       map[uint32]*localStream{},
	}

	for _, opt := range r.opts {
		if err := opt(i); err != nil {
			return nil, err
		}
	}

	if i.packetFactory == nil {
		i.packetFactory = newPacketManager()
	}

	if _, err := newSendBuffer(i.size); err != nil {
		return nil, err
	}

	return i, nil
}

// ResponderInterceptor responds to nack feedback messages
type ResponderInterceptor struct {
	interceptor.NoOp
	streamsFilter func(info *interceptor.StreamInfo) bool
	size          uint16
	log           logging.LeveledLogger
	packetFactory packetFactory

	streams   map[uint32]*localStream
	streamsMu sync.Mutex
}

type localStream struct {
	sendBuffer *sendBuffer
	rtpWriter  interceptor.RTPWriter
}

// NewResponderInterceptor returns a new ResponderInterceptorFactor
func NewResponderInterceptor(opts ...ResponderOption) (*ResponderInterceptorFactory, error) {
	return &ResponderInterceptorFactory{opts}, nil
}

// BindRTCPReader lets you modify any incoming RTCP packets. It is called once per sender/receiver, however this might
// change in the future. The returned method will be called once per packet batch.
func (n *ResponderInterceptor) BindRTCPReader(reader interceptor.RTCPReader) interceptor.RTCPReader {
	return interceptor.RTCPReaderFunc(func(b []byte, a interceptor.Attributes) (int, interceptor.Attributes, error) {
		i, attr, err := reader.Read(b, a)
		if err != nil {
			return 0, nil, err
		}

		if attr == nil {
			attr = make(interceptor.Attributes)
		}
		pkts, err := attr.GetRTCPPackets(b[:i])
		if err != nil {
			return 0, nil, err
		}
		for _, rtcpPacket := range pkts {
			nack, ok := rtcpPacket.(*rtcp.TransportLayerNack)
			if !ok {
				continue
			}

			go n.resendPackets(nack)
		}

		return i, attr, err
	})
}

// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
// will be called once per rtp packet.
func (n *ResponderInterceptor) BindLocalStream(info *interceptor.StreamInfo, writer interceptor.RTPWriter) interceptor.RTPWriter {
	if !n.streamsFilter(info) {
		return writer
	}

	// error is already checked in NewGeneratorInterceptor
	sendBuffer, _ := newSendBuffer(n.size)
	n.streamsMu.Lock()
	n.streams[info.SSRC] = &localStream{
		sendBuffer: sendBuffer,
		rtpWriter:  writer,
	}
	n.streamsMu.Unlock()

	return interceptor.RTPWriterFunc(func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
		pkt, err := n.packetFactory.NewPacket(header, payload, info.SSRCRetransmission, info.PayloadTypeRetransmission)
		if err != nil {
			return 0, err
		}
		sendBuffer.add(pkt)
		return writer.Write(header, payload, attributes)
	})
}

// UnbindLocalStream is called when the Stream is removed. It can be used to clean up any data related to that track.
func (n *ResponderInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
	n.streamsMu.Lock()
	delete(n.streams, info.SSRC)
	n.streamsMu.Unlock()
}

func (n *ResponderInterceptor) resendPackets(nack *rtcp.TransportLayerNack) {
	n.streamsMu.Lock()
	stream, ok := n.streams[nack.MediaSSRC]
	n.streamsMu.Unlock()
	if !ok {
		return
	}

	for i := range nack.Nacks {
		nack.Nacks[i].Range(func(seq uint16) bool {
			if p := stream.sendBuffer.get(seq); p != nil {
				if _, err := stream.rtpWriter.Write(p.Header(), p.Payload(), interceptor.Attributes{}); err != nil {
					n.log.Warnf("failed resending nacked packet: %+v", err)
				}
				p.Release()
			}

			return true
		})
	}
}
