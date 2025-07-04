// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"strings"
	"sync"

	"github.com/pion/rtp"
	"github.com/pion/webrtc/v4/internal/util"
	"github.com/pion/webrtc/v4/pkg/media"
)

// trackBinding is a single bind for a Track
// Bind can be called multiple times, this stores the
// result for a single bind call so that it can be used when writing.
type trackBinding struct {
	id                          string
	ssrc, ssrcRTX, ssrcFEC      SSRC
	payloadType, payloadTypeRTX PayloadType
	writeStream                 TrackLocalWriter
}

// TrackLocalStaticRTP  is a TrackLocal that has a pre-set codec and accepts RTP Packets.
// If you wish to send a media.Sample use TrackLocalStaticSample.
type TrackLocalStaticRTP struct {
	mu                sync.RWMutex
	bindings          []trackBinding
	codec             RTPCodecCapability
	payloader         func(RTPCodecCapability) (rtp.Payloader, error)
	id, rid, streamID string
	rtpTimestamp      *uint32
}

// NewTrackLocalStaticRTP returns a TrackLocalStaticRTP.
func NewTrackLocalStaticRTP(
	c RTPCodecCapability,
	id, streamID string,
	options ...func(*TrackLocalStaticRTP),
) (*TrackLocalStaticRTP, error) {
	t := &TrackLocalStaticRTP{
		codec:    c,
		bindings: []trackBinding{},
		id:       id,
		streamID: streamID,
	}

	for _, option := range options {
		option(t)
	}

	return t, nil
}

// WithRTPStreamID sets the RTP stream ID for this TrackLocalStaticRTP.
func WithRTPStreamID(rid string) func(*TrackLocalStaticRTP) {
	return func(t *TrackLocalStaticRTP) {
		t.rid = rid
	}
}

// WithPayloader allows the user to override the Payloader.
func WithPayloader(h func(RTPCodecCapability) (rtp.Payloader, error)) func(*TrackLocalStaticRTP) {
	return func(s *TrackLocalStaticRTP) {
		s.payloader = h
	}
}

// WithRTPTimestamp set the initial RTP timestamp for the track.
func WithRTPTimestamp(timestamp uint32) func(*TrackLocalStaticRTP) {
	return func(s *TrackLocalStaticRTP) {
		s.rtpTimestamp = &timestamp
	}
}

// Bind is called by the PeerConnection after negotiation is complete
// This asserts that the code requested is supported by the remote peer.
// If so it sets up all the state (SSRC and PayloadType) to have a call.
func (s *TrackLocalStaticRTP) Bind(trackContext TrackLocalContext) (RTPCodecParameters, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	parameters := RTPCodecParameters{RTPCodecCapability: s.codec}
	if codec, matchType := codecParametersFuzzySearch(
		parameters,
		trackContext.CodecParameters(),
	); matchType != codecMatchNone {
		s.bindings = append(s.bindings, trackBinding{
			ssrc:           trackContext.SSRC(),
			ssrcRTX:        trackContext.SSRCRetransmission(),
			ssrcFEC:        trackContext.SSRCForwardErrorCorrection(),
			payloadType:    codec.PayloadType,
			payloadTypeRTX: findRTXPayloadType(codec.PayloadType, trackContext.CodecParameters()),
			writeStream:    trackContext.WriteStream(),
			id:             trackContext.ID(),
		})

		return codec, nil
	}

	return RTPCodecParameters{}, ErrUnsupportedCodec
}

// Unbind implements the teardown logic when the track is no longer needed. This happens
// because a track has been stopped.
func (s *TrackLocalStaticRTP) Unbind(t TrackLocalContext) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for i := range s.bindings {
		if s.bindings[i].id == t.ID() {
			s.bindings[i] = s.bindings[len(s.bindings)-1]
			s.bindings = s.bindings[:len(s.bindings)-1]

			return nil
		}
	}

	return ErrUnbindFailed
}

// ID is the unique identifier for this Track. This should be unique for the
// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
// and StreamID would be 'desktop' or 'webcam'.
func (s *TrackLocalStaticRTP) ID() string { return s.id }

// StreamID is the group this track belongs too. This must be unique.
func (s *TrackLocalStaticRTP) StreamID() string { return s.streamID }

// RID is the RTP stream identifier.
func (s *TrackLocalStaticRTP) RID() string { return s.rid }

// Kind controls if this TrackLocal is audio or video.
func (s *TrackLocalStaticRTP) Kind() RTPCodecType {
	switch {
	case strings.HasPrefix(s.codec.MimeType, "audio/"):
		return RTPCodecTypeAudio
	case strings.HasPrefix(s.codec.MimeType, "video/"):
		return RTPCodecTypeVideo
	default:
		return RTPCodecType(0)
	}
}

// Codec gets the Codec of the track.
func (s *TrackLocalStaticRTP) Codec() RTPCodecCapability {
	return s.codec
}

// packetPool is a pool of packets used by WriteRTP and Write below
// nolint:gochecknoglobals
var rtpPacketPool = sync.Pool{
	New: func() any {
		return &rtp.Packet{}
	},
}

func resetPacketPoolAllocation(localPacket *rtp.Packet) {
	*localPacket = rtp.Packet{}
	rtpPacketPool.Put(localPacket)
}

func getPacketAllocationFromPool() *rtp.Packet {
	ipacket := rtpPacketPool.Get()

	return ipacket.(*rtp.Packet) //nolint:forcetypeassert
}

// WriteRTP writes a RTP Packet to the TrackLocalStaticRTP
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them.
func (s *TrackLocalStaticRTP) WriteRTP(p *rtp.Packet) error {
	packet := getPacketAllocationFromPool()

	defer resetPacketPoolAllocation(packet)

	*packet = *p

	return s.writeRTP(packet)
}

// writeRTP is like WriteRTP, except that it may modify the packet p.
func (s *TrackLocalStaticRTP) writeRTP(packet *rtp.Packet) error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	writeErrs := []error{}

	for _, b := range s.bindings {
		packet.Header.SSRC = uint32(b.ssrc)
		packet.Header.PayloadType = uint8(b.payloadType)
		// b.writeStream.WriteRTP below expects header and payload separately, so value of Packet.PaddingSize
		// would be lost. Copy it to Packet.Header.PaddingSize to avoid that problem.
		if packet.PaddingSize != 0 && packet.Header.PaddingSize == 0 {
			packet.Header.PaddingSize = packet.PaddingSize
		}
		if _, err := b.writeStream.WriteRTP(&packet.Header, packet.Payload); err != nil {
			writeErrs = append(writeErrs, err)
		}
	}

	return util.FlattenErrs(writeErrs)
}

// Write writes a RTP Packet as a buffer to the TrackLocalStaticRTP
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them.
func (s *TrackLocalStaticRTP) Write(b []byte) (n int, err error) {
	packet := getPacketAllocationFromPool()

	defer resetPacketPoolAllocation(packet)

	if err = packet.Unmarshal(b); err != nil {
		return 0, err
	}

	return len(b), s.writeRTP(packet)
}

// TrackLocalStaticSample is a TrackLocal that has a pre-set codec and accepts Samples.
// If you wish to send a RTP Packet use TrackLocalStaticRTP.
type TrackLocalStaticSample struct {
	packetizer rtp.Packetizer
	sequencer  rtp.Sequencer
	rtpTrack   *TrackLocalStaticRTP
	clockRate  float64
}

// NewTrackLocalStaticSample returns a TrackLocalStaticSample.
func NewTrackLocalStaticSample(
	c RTPCodecCapability,
	id, streamID string,
	options ...func(*TrackLocalStaticRTP),
) (*TrackLocalStaticSample, error) {
	rtpTrack, err := NewTrackLocalStaticRTP(c, id, streamID, options...)
	if err != nil {
		return nil, err
	}

	return &TrackLocalStaticSample{
		rtpTrack: rtpTrack,
	}, nil
}

// ID is the unique identifier for this Track. This should be unique for the
// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
// and StreamID would be 'desktop' or 'webcam'.
func (s *TrackLocalStaticSample) ID() string { return s.rtpTrack.ID() }

// StreamID is the group this track belongs too. This must be unique.
func (s *TrackLocalStaticSample) StreamID() string { return s.rtpTrack.StreamID() }

// RID is the RTP stream identifier.
func (s *TrackLocalStaticSample) RID() string { return s.rtpTrack.RID() }

// Kind controls if this TrackLocal is audio or video.
func (s *TrackLocalStaticSample) Kind() RTPCodecType { return s.rtpTrack.Kind() }

// Codec gets the Codec of the track.
func (s *TrackLocalStaticSample) Codec() RTPCodecCapability {
	return s.rtpTrack.Codec()
}

// Bind is called by the PeerConnection after negotiation is complete
// This asserts that the code requested is supported by the remote peer.
// If so it setups all the state (SSRC and PayloadType) to have a call.
func (s *TrackLocalStaticSample) Bind(t TrackLocalContext) (RTPCodecParameters, error) {
	codec, err := s.rtpTrack.Bind(t)
	if err != nil {
		return codec, err
	}

	s.rtpTrack.mu.Lock()
	defer s.rtpTrack.mu.Unlock()

	// We only need one packetizer
	if s.packetizer != nil {
		return codec, nil
	}

	payloadHandler := s.rtpTrack.payloader
	if payloadHandler == nil {
		payloadHandler = payloaderForCodec
	}

	payloader, err := payloadHandler(codec.RTPCodecCapability)
	if err != nil {
		return codec, err
	}

	s.sequencer = rtp.NewRandomSequencer()

	options := []rtp.PacketizerOption{}

	if s.rtpTrack.rtpTimestamp != nil {
		options = append(options, rtp.WithTimestamp(*s.rtpTrack.rtpTimestamp))
	}

	s.packetizer = rtp.NewPacketizerWithOptions(
		outboundMTU,
		payloader,
		s.sequencer,
		codec.ClockRate,
		options...,
	)

	s.clockRate = float64(codec.RTPCodecCapability.ClockRate)

	return codec, nil
}

// Unbind implements the teardown logic when the track is no longer needed. This happens
// because a track has been stopped.
func (s *TrackLocalStaticSample) Unbind(t TrackLocalContext) error {
	return s.rtpTrack.Unbind(t)
}

// WriteSample writes a Sample to the TrackLocalStaticSample
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them.
func (s *TrackLocalStaticSample) WriteSample(sample media.Sample) error {
	s.rtpTrack.mu.RLock()
	packetizer := s.packetizer
	clockRate := s.clockRate
	s.rtpTrack.mu.RUnlock()

	if packetizer == nil {
		return nil
	}

	// skip packets by the number of previously dropped packets
	for i := uint16(0); i < sample.PrevDroppedPackets; i++ {
		s.sequencer.NextSequenceNumber()
	}

	samples := uint32(sample.Duration.Seconds() * clockRate)
	if sample.PrevDroppedPackets > 0 {
		packetizer.SkipSamples(samples * uint32(sample.PrevDroppedPackets))
	}
	packets := packetizer.Packetize(sample.Data, samples)

	writeErrs := []error{}
	for _, p := range packets {
		if err := s.rtpTrack.WriteRTP(p); err != nil {
			writeErrs = append(writeErrs, err)
		}
	}

	return util.FlattenErrs(writeErrs)
}

// GeneratePadding writes padding-only samples to the TrackLocalStaticSample
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them.
func (s *TrackLocalStaticSample) GeneratePadding(samples uint32) error {
	s.rtpTrack.mu.RLock()
	p := s.packetizer
	s.rtpTrack.mu.RUnlock()

	if p == nil {
		return nil
	}

	packets := p.GeneratePadding(samples)

	writeErrs := []error{}
	for _, p := range packets {
		if err := s.rtpTrack.WriteRTP(p); err != nil {
			writeErrs = append(writeErrs, err)
		}
	}

	return util.FlattenErrs(writeErrs)
}
