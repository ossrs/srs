// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"sync"
	"time"

	"github.com/pion/interceptor"
	"github.com/pion/rtp"
)

// TrackRemote represents a single inbound source of media
type TrackRemote struct {
	mu sync.RWMutex

	id       string
	streamID string

	payloadType PayloadType
	kind        RTPCodecType
	ssrc        SSRC
	codec       RTPCodecParameters
	params      RTPParameters
	rid         string

	receiver         *RTPReceiver
	peeked           []byte
	peekedAttributes interceptor.Attributes
}

func newTrackRemote(kind RTPCodecType, ssrc SSRC, rid string, receiver *RTPReceiver) *TrackRemote {
	return &TrackRemote{
		kind:     kind,
		ssrc:     ssrc,
		rid:      rid,
		receiver: receiver,
	}
}

// ID is the unique identifier for this Track. This should be unique for the
// stream, but doesn't have to globally unique. A common example would be 'audio' or 'video'
// and StreamID would be 'desktop' or 'webcam'
func (t *TrackRemote) ID() string {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.id
}

// RID gets the RTP Stream ID of this Track
// With Simulcast you will have multiple tracks with the same ID, but different RID values.
// In many cases a TrackRemote will not have an RID, so it is important to assert it is non-zero
func (t *TrackRemote) RID() string {
	t.mu.RLock()
	defer t.mu.RUnlock()

	return t.rid
}

// PayloadType gets the PayloadType of the track
func (t *TrackRemote) PayloadType() PayloadType {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.payloadType
}

// Kind gets the Kind of the track
func (t *TrackRemote) Kind() RTPCodecType {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.kind
}

// StreamID is the group this track belongs too. This must be unique
func (t *TrackRemote) StreamID() string {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.streamID
}

// SSRC gets the SSRC of the track
func (t *TrackRemote) SSRC() SSRC {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.ssrc
}

// Msid gets the Msid of the track
func (t *TrackRemote) Msid() string {
	return t.StreamID() + " " + t.ID()
}

// Codec gets the Codec of the track
func (t *TrackRemote) Codec() RTPCodecParameters {
	t.mu.RLock()
	defer t.mu.RUnlock()
	return t.codec
}

// Read reads data from the track.
func (t *TrackRemote) Read(b []byte) (n int, attributes interceptor.Attributes, err error) {
	t.mu.RLock()
	r := t.receiver
	peeked := t.peeked != nil
	t.mu.RUnlock()

	if peeked {
		t.mu.Lock()
		data := t.peeked
		attributes = t.peekedAttributes

		t.peeked = nil
		t.peekedAttributes = nil
		t.mu.Unlock()
		// someone else may have stolen our packet when we
		// released the lock.  Deal with it.
		if data != nil {
			n = copy(b, data)
			err = t.checkAndUpdateTrack(b)
			return
		}
	}

	n, attributes, err = r.readRTP(b, t)
	if err != nil {
		return
	}

	err = t.checkAndUpdateTrack(b)
	return
}

// checkAndUpdateTrack checks payloadType for every incoming packet
// once a different payloadType is detected the track will be updated
func (t *TrackRemote) checkAndUpdateTrack(b []byte) error {
	if len(b) < 2 {
		return errRTPTooShort
	}

	if payloadType := PayloadType(b[1] & rtpPayloadTypeBitmask); payloadType != t.PayloadType() {
		t.mu.Lock()
		defer t.mu.Unlock()

		params, err := t.receiver.api.mediaEngine.getRTPParametersByPayloadType(payloadType)
		if err != nil {
			return err
		}

		t.kind = t.receiver.kind
		t.payloadType = payloadType
		t.codec = params.Codecs[0]
		t.params = params
	}

	return nil
}

// ReadRTP is a convenience method that wraps Read and unmarshals for you.
func (t *TrackRemote) ReadRTP() (*rtp.Packet, interceptor.Attributes, error) {
	b := make([]byte, t.receiver.api.settingEngine.getReceiveMTU())
	i, attributes, err := t.Read(b)
	if err != nil {
		return nil, nil, err
	}

	r := &rtp.Packet{}
	if err := r.Unmarshal(b[:i]); err != nil {
		return nil, nil, err
	}
	return r, attributes, nil
}

// peek is like Read, but it doesn't discard the packet read
func (t *TrackRemote) peek(b []byte) (n int, a interceptor.Attributes, err error) {
	n, a, err = t.Read(b)
	if err != nil {
		return
	}

	t.mu.Lock()
	// this might overwrite data if somebody peeked between the Read
	// and us getting the lock.  Oh well, we'll just drop a packet in
	// that case.
	data := make([]byte, n)
	n = copy(data, b[:n])
	t.peeked = data
	t.peekedAttributes = a
	t.mu.Unlock()
	return
}

// SetReadDeadline sets the max amount of time the RTP stream will block before returning. 0 is forever.
func (t *TrackRemote) SetReadDeadline(deadline time.Time) error {
	return t.receiver.setRTPReadDeadline(deadline, t)
}
