// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

import (
	"github.com/pion/interceptor"
	"github.com/pion/rtp"
)

// TrackLocalWriter is the Writer for outbound RTP Packets.
type TrackLocalWriter interface {
	// WriteRTP encrypts a RTP packet and writes to the connection
	WriteRTP(header *rtp.Header, payload []byte) (int, error)

	// Write encrypts and writes a full RTP packet
	Write(b []byte) (int, error)
}

// TrackLocalContext is the Context passed when a TrackLocal has been Binded/Unbinded from a PeerConnection, and used
// in Interceptors.
type TrackLocalContext interface {
	// CodecParameters returns the negotiated RTPCodecParameters. These are the codecs supported by both
	// PeerConnections and the PayloadTypes
	CodecParameters() []RTPCodecParameters

	// HeaderExtensions returns the negotiated RTPHeaderExtensionParameters. These are the header extensions supported by
	// both PeerConnections and the URI/IDs
	HeaderExtensions() []RTPHeaderExtensionParameter

	// SSRC returns the negotiated SSRC of this track
	SSRC() SSRC

	// SSRCRetransmission returns the negotiated SSRC used to send retransmissions for this track
	SSRCRetransmission() SSRC

	// SSRCForwardErrorCorrection returns the negotiated SSRC to send forward error correction for this track
	SSRCForwardErrorCorrection() SSRC

	// WriteStream returns the WriteStream for this TrackLocal. The implementer writes the outbound
	// media packets to it
	WriteStream() TrackLocalWriter

	// ID is a unique identifier that is used for both Bind/Unbind
	ID() string

	// RTCPReader returns the RTCP interceptor for this TrackLocal. Used to read RTCP of this TrackLocal.
	RTCPReader() interceptor.RTCPReader
}

type baseTrackLocalContext struct {
	id                     string
	params                 RTPParameters
	ssrc, ssrcRTX, ssrcFEC SSRC
	writeStream            TrackLocalWriter
	rtcpInterceptor        interceptor.RTCPReader
}

// CodecParameters returns the negotiated RTPCodecParameters. These are the codecs supported by both
// PeerConnections and the SSRC/PayloadTypes.
func (t *baseTrackLocalContext) CodecParameters() []RTPCodecParameters {
	return t.params.Codecs
}

// HeaderExtensions returns the negotiated RTPHeaderExtensionParameters. These are the header extensions supported by
// both PeerConnections and the SSRC/PayloadTypes.
func (t *baseTrackLocalContext) HeaderExtensions() []RTPHeaderExtensionParameter {
	return t.params.HeaderExtensions
}

// SSRC requires the negotiated SSRC of this track.
func (t *baseTrackLocalContext) SSRC() SSRC {
	return t.ssrc
}

// SSRCRetransmission returns the negotiated SSRC used to send retransmissions for this track.
func (t *baseTrackLocalContext) SSRCRetransmission() SSRC {
	return t.ssrcRTX
}

// SSRCForwardErrorCorrection returns the negotiated SSRC to send forward error correction for this track.
func (t *baseTrackLocalContext) SSRCForwardErrorCorrection() SSRC {
	return t.ssrcFEC
}

// WriteStream returns the WriteStream for this TrackLocal. The implementer writes the outbound
// media packets to it.
func (t *baseTrackLocalContext) WriteStream() TrackLocalWriter {
	return t.writeStream
}

// ID is a unique identifier that is used for both Bind/Unbind.
func (t *baseTrackLocalContext) ID() string {
	return t.id
}

// RTCPReader returns the RTCP interceptor for this TrackLocal. Used to read RTCP of this TrackLocal.
func (t *baseTrackLocalContext) RTCPReader() interceptor.RTCPReader {
	return t.rtcpInterceptor
}

// TrackLocal is an interface that controls how the user can send media
// The user can provide their own TrackLocal implementations, or use
// the implementations in pkg/media.
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

	// RID is the RTP Stream ID for this track.
	RID() string

	// StreamID is the group this track belongs too. This must be unique
	StreamID() string

	// Kind controls if this TrackLocal is audio or video
	Kind() RTPCodecType
}
