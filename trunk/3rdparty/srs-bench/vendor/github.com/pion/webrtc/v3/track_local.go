package webrtc

import "github.com/pion/rtp"

// TrackLocalWriter is the Writer for outbound RTP Packets
type TrackLocalWriter interface {
	// WriteRTP encrypts a RTP packet and writes to the connection
	WriteRTP(header *rtp.Header, payload []byte) (int, error)

	// Write encrypts and writes a full RTP packet
	Write(b []byte) (int, error)
}

// TrackLocalContext is the Context passed when a TrackLocal has been Binded/Unbinded from a PeerConnection, and used
// in Interceptors.
type TrackLocalContext struct {
	id          string
	params      RTPParameters
	ssrc        SSRC
	writeStream TrackLocalWriter
}

// CodecParameters returns the negotiated RTPCodecParameters. These are the codecs supported by both
// PeerConnections and the SSRC/PayloadTypes
func (t *TrackLocalContext) CodecParameters() []RTPCodecParameters {
	return t.params.Codecs
}

// HeaderExtensions returns the negotiated RTPHeaderExtensionParameters. These are the header extensions supported by
// both PeerConnections and the SSRC/PayloadTypes
func (t *TrackLocalContext) HeaderExtensions() []RTPHeaderExtensionParameter {
	return t.params.HeaderExtensions
}

// SSRC requires the negotiated SSRC of this track
// This track may have multiple if RTX is enabled
func (t *TrackLocalContext) SSRC() SSRC {
	return t.ssrc
}

// WriteStream returns the WriteStream for this TrackLocal. The implementer writes the outbound
// media packets to it
func (t *TrackLocalContext) WriteStream() TrackLocalWriter {
	return t.writeStream
}

// ID is a unique identifier that is used for both Bind/Unbind
func (t *TrackLocalContext) ID() string {
	return t.id
}

// TrackLocal is an interface that controls how the user can send media
// The user can provide their own TrackLocal implementatiosn, or use
// the implementations in pkg/media
type TrackLocal interface {
	// Bind should implement the way how the media data flows from the Track to the PeerConnection
	// This will be called internally after signaling is complete and the list of available
	// codecs has been determined
	Bind(TrackLocalContext) (RTPCodecParameters, error)

	// Unbind should implement the teardown logic when the track is no longer needed. This happens
	// because a track has been stopped.
	Unbind(TrackLocalContext) error

	// ID is the unique identifier for this Track. This should be unique for the
	// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
	// and StreamID would be 'desktop' or 'webcam'
	ID() string

	// StreamID is the group this track belongs too. This must be unique
	StreamID() string

	// Kind controls if this TrackLocal is audio or video
	Kind() RTPCodecType
}
