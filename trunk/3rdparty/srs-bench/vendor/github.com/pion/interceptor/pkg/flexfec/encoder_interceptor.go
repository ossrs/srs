// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package flexfec

import (
	"errors"
	"sync"

	"github.com/pion/interceptor"
	"github.com/pion/rtp"
)

// streamState holds the state for a single stream.
type streamState struct {
	mu             sync.Mutex
	flexFecEncoder FlexEncoder
	packetBuffer   []rtp.Packet
}

// FecInterceptor implements FlexFec.
type FecInterceptor struct {
	interceptor.NoOp
	mu              sync.Mutex
	streams         map[uint32]*streamState
	numMediaPackets uint32
	numFecPackets   uint32
	encoderFactory  EncoderFactory
}

// FecInterceptorFactory creates new FecInterceptors.
type FecInterceptorFactory struct {
	opts []FecOption
}

// NewFecInterceptor returns a new Fec interceptor factory.
func NewFecInterceptor(opts ...FecOption) (*FecInterceptorFactory, error) {
	return &FecInterceptorFactory{opts: opts}, nil
}

// NewInterceptor constructs a new FecInterceptor.
func (r *FecInterceptorFactory) NewInterceptor(_ string) (interceptor.Interceptor, error) {
	interceptor := &FecInterceptor{
		streams:         make(map[uint32]*streamState),
		numMediaPackets: 5,
		numFecPackets:   2,
		encoderFactory:  FlexEncoder03Factory{},
	}

	for _, opt := range r.opts {
		if err := opt(interceptor); err != nil {
			return nil, err
		}
	}

	return interceptor, nil
}

// UnbindLocalStream removes the stream state for a specific SSRC.
func (r *FecInterceptor) UnbindLocalStream(info *interceptor.StreamInfo) {
	r.mu.Lock()
	defer r.mu.Unlock()

	delete(r.streams, info.SSRC)
}

// BindLocalStream lets you modify any outgoing RTP packets. It is called once for per LocalStream. The returned method
// will be called once per rtp packet.
func (r *FecInterceptor) BindLocalStream(
	info *interceptor.StreamInfo, writer interceptor.RTPWriter,
) interceptor.RTPWriter {
	if info.PayloadTypeForwardErrorCorrection == 0 || info.SSRCForwardErrorCorrection == 0 {
		return writer
	}

	mediaSSRC := info.SSRC

	r.mu.Lock()
	stream := &streamState{
		// Chromium supports version flexfec-03 of existing draft, this is the one we will configure by default
		// although we should support configuring the latest (flexfec-20) as well.
		flexFecEncoder: r.encoderFactory.NewEncoder(info.PayloadTypeForwardErrorCorrection, info.SSRCForwardErrorCorrection),
		packetBuffer:   make([]rtp.Packet, 0),
	}
	r.streams[mediaSSRC] = stream
	r.mu.Unlock()

	return interceptor.RTPWriterFunc(
		func(header *rtp.Header, payload []byte, attributes interceptor.Attributes) (int, error) {
			// Ignore non-media packets
			if header.SSRC != mediaSSRC {
				return writer.Write(header, payload, attributes)
			}

			var fecPackets []rtp.Packet
			stream.mu.Lock()
			stream.packetBuffer = append(stream.packetBuffer, rtp.Packet{
				Header:  *header,
				Payload: payload,
			})

			// Check if we have enough packets to generate FEC
			if len(stream.packetBuffer) == int(r.numMediaPackets) {
				fecPackets = stream.flexFecEncoder.EncodeFec(stream.packetBuffer, r.numFecPackets)
				// Reset the packet buffer now that we've sent the corresponding FEC packets.
				stream.packetBuffer = nil
			}
			stream.mu.Unlock()

			var errs []error
			result, err := writer.Write(header, payload, attributes)
			if err != nil {
				errs = append(errs, err)
			}

			for _, packet := range fecPackets {
				header := packet.Header

				_, err = writer.Write(&header, packet.Payload, attributes)
				if err != nil {
					errs = append(errs, err)
				}
			}

			return result, errors.Join(errs...)
		},
	)
}
