package rtc

import (
	"github.com/pion/rtp"
	"github.com/pion/webrtc/v3"
	"github.com/pion/webrtc/v3/pkg/media"
	"strings"
	"sync"
)

// trackBinding is a single bind for a Track
// Bind can be called multiple times, this stores the
// result for a single bind call so that it can be used when writing
type trackBinding struct {
	id          string
	ssrc        webrtc.SSRC
	payloadType webrtc.PayloadType
	writeStream webrtc.TrackLocalWriter
}

// TrackLocalStaticRTP  is a TrackLocal that has a pre-set codec and accepts RTP Packets.
// If you wish to send a media.Sample use TrackLocalStaticSample
type TrackLocalStaticRTP struct {
	mu           sync.RWMutex
	bindings     []trackBinding
	codec        webrtc.RTPCodecCapability
	id, streamID string
}

// NewTrackLocalStaticRTP returns a TrackLocalStaticRTP.
func NewTrackLocalStaticRTP(c webrtc.RTPCodecCapability, id, streamID string) (*TrackLocalStaticRTP, error) {
	return &TrackLocalStaticRTP{
		codec:    c,
		bindings: []trackBinding{},
		id:       id,
		streamID: streamID,
	}, nil
}

// Bind is called by the PeerConnection after negotiation is complete
// This asserts that the code requested is supported by the remote peer.
// If so it setups all the state (SSRC and PayloadType) to have a call
func (s *TrackLocalStaticRTP) Bind(t webrtc.TrackLocalContext) (webrtc.RTPCodecParameters, error) {
	s.mu.Lock()
	defer s.mu.Unlock()

	parameters := webrtc.RTPCodecParameters{RTPCodecCapability: s.codec}
	if codec, err := codecParametersFuzzySearch(parameters, t.CodecParameters()); err == nil {
		s.bindings = append(s.bindings, trackBinding{
			ssrc:        t.SSRC(),
			payloadType: codec.PayloadType,
			writeStream: t.WriteStream(),
			id:          t.ID(),
		})
		return codec, nil
	}

	return webrtc.RTPCodecParameters{}, webrtc.ErrUnsupportedCodec
}

// Unbind implements the teardown logic when the track is no longer needed. This happens
// because a track has been stopped.
func (s *TrackLocalStaticRTP) Unbind(t webrtc.TrackLocalContext) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for i := range s.bindings {
		if s.bindings[i].id == t.ID() {
			s.bindings[i] = s.bindings[len(s.bindings)-1]
			s.bindings = s.bindings[:len(s.bindings)-1]
			return nil
		}
	}

	return webrtc.ErrUnbindFailed
}

// ID is the unique identifier for this Track. This should be unique for the
// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
// and StreamID would be 'desktop' or 'webcam'
func (s *TrackLocalStaticRTP) ID() string { return s.id }

// StreamID is the group this track belongs too. This must be unique
func (s *TrackLocalStaticRTP) StreamID() string { return s.streamID }

// Kind controls if this TrackLocal is audio or video
func (s *TrackLocalStaticRTP) Kind() webrtc.RTPCodecType {
	switch {
	case strings.HasPrefix(s.codec.MimeType, "audio/"):
		return webrtc.RTPCodecTypeAudio
	case strings.HasPrefix(s.codec.MimeType, "video/"):
		return webrtc.RTPCodecTypeVideo
	default:
		return webrtc.RTPCodecType(0)
	}
}

// Codec gets the Codec of the track
func (s *TrackLocalStaticRTP) Codec() webrtc.RTPCodecCapability {
	return s.codec
}

// WriteRTP writes a RTP Packet to the TrackLocalStaticRTP
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them
func (s *TrackLocalStaticRTP) WriteRTP(p *rtp.Packet) error {
	s.mu.RLock()
	defer s.mu.RUnlock()

	writeErrs := []error{}
	outboundPacket := *p

	for _, b := range s.bindings {
		outboundPacket.Header.SSRC = uint32(b.ssrc)
		outboundPacket.Header.PayloadType = uint8(b.payloadType)
		if _, err := b.writeStream.WriteRTP(&outboundPacket.Header, outboundPacket.Payload); err != nil {
			writeErrs = append(writeErrs, err)
		}
	}

	return FlattenErrs(writeErrs)
}

// Write writes a RTP Packet as a buffer to the TrackLocalStaticRTP
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them
func (s *TrackLocalStaticRTP) Write(b []byte) (n int, err error) {
	packet := &rtp.Packet{}
	if err = packet.Unmarshal(b); err != nil {
		return 0, err
	}

	return len(b), s.WriteRTP(packet)
}

// TrackLocalStaticSample is a TrackLocal that has a pre-set codec and accepts Samples.
// If you wish to send a RTP Packet use TrackLocalStaticRTP
type TrackLocalStaticSample struct {
	packetizer rtp.Packetizer
	rtpTrack   *TrackLocalStaticRTP
	clockRate  float64

	// Set the callback before write RTP packet.
	OnBeforeWritePacket func(rtp *rtp.Packet)
}

// NewTrackLocalStaticSample returns a TrackLocalStaticSample
func NewTrackLocalStaticSample(c webrtc.RTPCodecCapability, id, streamID string) (*TrackLocalStaticSample, error) {
	rtpTrack, err := NewTrackLocalStaticRTP(c, id, streamID)
	if err != nil {
		return nil, err
	}

	return &TrackLocalStaticSample{
		rtpTrack: rtpTrack,
	}, nil
}

// ID is the unique identifier for this Track. This should be unique for the
// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
// and StreamID would be 'desktop' or 'webcam'
func (s *TrackLocalStaticSample) ID() string { return s.rtpTrack.ID() }

// StreamID is the group this track belongs too. This must be unique
func (s *TrackLocalStaticSample) StreamID() string { return s.rtpTrack.StreamID() }

// Kind controls if this TrackLocal is audio or video
func (s *TrackLocalStaticSample) Kind() webrtc.RTPCodecType { return s.rtpTrack.Kind() }

// Codec gets the Codec of the track
func (s *TrackLocalStaticSample) Codec() webrtc.RTPCodecCapability {
	return s.rtpTrack.Codec()
}

// Bind is called by the PeerConnection after negotiation is complete
// This asserts that the code requested is supported by the remote peer.
// If so it setups all the state (SSRC and PayloadType) to have a call
func (s *TrackLocalStaticSample) Bind(t webrtc.TrackLocalContext) (webrtc.RTPCodecParameters, error) {
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

	payloader, err := payloaderForCodec(codec.RTPCodecCapability)
	if err != nil {
		return codec, err
	}

	s.packetizer = rtp.NewPacketizer(
		rtpOutboundMTU,
		0, // Value is handled when writing
		0, // Value is handled when writing
		payloader,
		rtp.NewRandomSequencer(),
		codec.ClockRate,
	)
	s.clockRate = float64(codec.RTPCodecCapability.ClockRate)
	return codec, nil
}

// Unbind implements the teardown logic when the track is no longer needed. This happens
// because a track has been stopped.
func (s *TrackLocalStaticSample) Unbind(t webrtc.TrackLocalContext) error {
	return s.rtpTrack.Unbind(t)
}

// WriteSample writes a Sample to the TrackLocalStaticSample
// If one PeerConnection fails the packets will still be sent to
// all PeerConnections. The error message will contain the ID of the failed
// PeerConnections so you can remove them
func (s *TrackLocalStaticSample) WriteSample(sample media.Sample) error {
	s.rtpTrack.mu.RLock()
	p := s.packetizer
	clockRate := s.clockRate
	s.rtpTrack.mu.RUnlock()

	if p == nil {
		return nil
	}

	samples := sample.Duration.Seconds() * clockRate
	packets := p.(rtp.Packetizer).Packetize(sample.Data, uint32(samples))

	writeErrs := []error{}
	for _, p := range packets {
		if s.OnBeforeWritePacket != nil {
			s.OnBeforeWritePacket(p)
		}

		if err := s.rtpTrack.WriteRTP(p); err != nil {
			writeErrs = append(writeErrs, err)
		}
	}

	return FlattenErrs(writeErrs)
}
