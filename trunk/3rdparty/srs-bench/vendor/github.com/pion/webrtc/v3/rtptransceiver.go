// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import (
	"fmt"
	"sync"
	"sync/atomic"

	"github.com/pion/rtp"
)

// RTPTransceiver represents a combination of an RTPSender and an RTPReceiver that share a common mid.
type RTPTransceiver struct {
	mid              atomic.Value // string
	sender           atomic.Value // *RTPSender
	receiver         atomic.Value // *RTPReceiver
	direction        atomic.Value // RTPTransceiverDirection
	currentDirection atomic.Value // RTPTransceiverDirection

	codecs []RTPCodecParameters // User provided codecs via SetCodecPreferences

	stopped bool
	kind    RTPCodecType

	api *API
	mu  sync.RWMutex
}

func newRTPTransceiver(
	receiver *RTPReceiver,
	sender *RTPSender,
	direction RTPTransceiverDirection,
	kind RTPCodecType,
	api *API,
) *RTPTransceiver {
	t := &RTPTransceiver{kind: kind, api: api}
	t.setReceiver(receiver)
	t.setSender(sender)
	t.setDirection(direction)
	t.setCurrentDirection(RTPTransceiverDirection(Unknown))
	return t
}

// SetCodecPreferences sets preferred list of supported codecs
// if codecs is empty or nil we reset to default from MediaEngine
func (t *RTPTransceiver) SetCodecPreferences(codecs []RTPCodecParameters) error {
	t.mu.Lock()
	defer t.mu.Unlock()

	for _, codec := range codecs {
		if _, matchType := codecParametersFuzzySearch(codec, t.api.mediaEngine.getCodecsByKind(t.kind)); matchType == codecMatchNone {
			return fmt.Errorf("%w %s", errRTPTransceiverCodecUnsupported, codec.MimeType)
		}
	}

	t.codecs = codecs
	return nil
}

// Codecs returns list of supported codecs
func (t *RTPTransceiver) getCodecs() []RTPCodecParameters {
	t.mu.RLock()
	defer t.mu.RUnlock()

	mediaEngineCodecs := t.api.mediaEngine.getCodecsByKind(t.kind)
	if len(t.codecs) == 0 {
		return mediaEngineCodecs
	}

	filteredCodecs := []RTPCodecParameters{}
	for _, codec := range t.codecs {
		if c, matchType := codecParametersFuzzySearch(codec, mediaEngineCodecs); matchType != codecMatchNone {
			if codec.PayloadType == 0 {
				codec.PayloadType = c.PayloadType
			}
			filteredCodecs = append(filteredCodecs, codec)
		}
	}

	return filteredCodecs
}

// Sender returns the RTPTransceiver's RTPSender if it has one
func (t *RTPTransceiver) Sender() *RTPSender {
	if v, ok := t.sender.Load().(*RTPSender); ok {
		return v
	}

	return nil
}

// SetSender sets the RTPSender and Track to current transceiver
func (t *RTPTransceiver) SetSender(s *RTPSender, track TrackLocal) error {
	t.setSender(s)
	return t.setSendingTrack(track)
}

func (t *RTPTransceiver) setSender(s *RTPSender) {
	if s != nil {
		s.setRTPTransceiver(t)
	}

	if prevSender := t.Sender(); prevSender != nil {
		prevSender.setRTPTransceiver(nil)
	}

	t.sender.Store(s)
}

// Receiver returns the RTPTransceiver's RTPReceiver if it has one
func (t *RTPTransceiver) Receiver() *RTPReceiver {
	if v, ok := t.receiver.Load().(*RTPReceiver); ok {
		return v
	}

	return nil
}

// SetMid sets the RTPTransceiver's mid. If it was already set, will return an error.
func (t *RTPTransceiver) SetMid(mid string) error {
	if currentMid := t.Mid(); currentMid != "" {
		return fmt.Errorf("%w: %s to %s", errRTPTransceiverCannotChangeMid, currentMid, mid)
	}
	t.mid.Store(mid)
	return nil
}

// Mid gets the Transceiver's mid value. When not already set, this value will be set in CreateOffer or CreateAnswer.
func (t *RTPTransceiver) Mid() string {
	if v, ok := t.mid.Load().(string); ok {
		return v
	}
	return ""
}

// Kind returns RTPTransceiver's kind.
func (t *RTPTransceiver) Kind() RTPCodecType {
	return t.kind
}

// Direction returns the RTPTransceiver's current direction
func (t *RTPTransceiver) Direction() RTPTransceiverDirection {
	if direction, ok := t.direction.Load().(RTPTransceiverDirection); ok {
		return direction
	}
	return RTPTransceiverDirection(0)
}

// Stop irreversibly stops the RTPTransceiver
func (t *RTPTransceiver) Stop() error {
	if sender := t.Sender(); sender != nil {
		if err := sender.Stop(); err != nil {
			return err
		}
	}
	if receiver := t.Receiver(); receiver != nil {
		if err := receiver.Stop(); err != nil {
			return err
		}
	}

	t.setDirection(RTPTransceiverDirectionInactive)
	t.setCurrentDirection(RTPTransceiverDirectionInactive)
	return nil
}

func (t *RTPTransceiver) setReceiver(r *RTPReceiver) {
	if r != nil {
		r.setRTPTransceiver(t)
	}

	if prevReceiver := t.Receiver(); prevReceiver != nil {
		prevReceiver.setRTPTransceiver(nil)
	}

	t.receiver.Store(r)
}

func (t *RTPTransceiver) setDirection(d RTPTransceiverDirection) {
	t.direction.Store(d)
}

func (t *RTPTransceiver) setCurrentDirection(d RTPTransceiverDirection) {
	t.currentDirection.Store(d)
}

func (t *RTPTransceiver) getCurrentDirection() RTPTransceiverDirection {
	if v, ok := t.currentDirection.Load().(RTPTransceiverDirection); ok {
		return v
	}
	return RTPTransceiverDirection(Unknown)
}

func (t *RTPTransceiver) setSendingTrack(track TrackLocal) error {
	if err := t.Sender().ReplaceTrack(track); err != nil {
		return err
	}
	if track == nil {
		t.setSender(nil)
	}

	switch {
	case track != nil && t.Direction() == RTPTransceiverDirectionRecvonly:
		t.setDirection(RTPTransceiverDirectionSendrecv)
	case track != nil && t.Direction() == RTPTransceiverDirectionInactive:
		t.setDirection(RTPTransceiverDirectionSendonly)
	case track == nil && t.Direction() == RTPTransceiverDirectionSendrecv:
		t.setDirection(RTPTransceiverDirectionRecvonly)
	case track != nil && t.Direction() == RTPTransceiverDirectionSendonly:
		// Handle the case where a sendonly transceiver was added by a negotiation
		// initiated by remote peer. For example a remote peer added a transceiver
		// with direction recvonly.
	case track != nil && t.Direction() == RTPTransceiverDirectionSendrecv:
		// Similar to above, but for sendrecv transceiver.
	case track == nil && t.Direction() == RTPTransceiverDirectionSendonly:
		t.setDirection(RTPTransceiverDirectionInactive)
	default:
		return errRTPTransceiverSetSendingInvalidState
	}
	return nil
}

func findByMid(mid string, localTransceivers []*RTPTransceiver) (*RTPTransceiver, []*RTPTransceiver) {
	for i, t := range localTransceivers {
		if t.Mid() == mid {
			return t, append(localTransceivers[:i], localTransceivers[i+1:]...)
		}
	}

	return nil, localTransceivers
}

// Given a direction+type pluck a transceiver from the passed list
// if no entry satisfies the requested type+direction return a inactive Transceiver
func satisfyTypeAndDirection(remoteKind RTPCodecType, remoteDirection RTPTransceiverDirection, localTransceivers []*RTPTransceiver) (*RTPTransceiver, []*RTPTransceiver) {
	// Get direction order from most preferred to least
	getPreferredDirections := func() []RTPTransceiverDirection {
		switch remoteDirection {
		case RTPTransceiverDirectionSendrecv:
			return []RTPTransceiverDirection{RTPTransceiverDirectionRecvonly, RTPTransceiverDirectionSendrecv, RTPTransceiverDirectionSendonly}
		case RTPTransceiverDirectionSendonly:
			return []RTPTransceiverDirection{RTPTransceiverDirectionRecvonly, RTPTransceiverDirectionSendrecv}
		case RTPTransceiverDirectionRecvonly:
			return []RTPTransceiverDirection{RTPTransceiverDirectionSendonly, RTPTransceiverDirectionSendrecv}
		default:
			return []RTPTransceiverDirection{}
		}
	}

	for _, possibleDirection := range getPreferredDirections() {
		for i := range localTransceivers {
			t := localTransceivers[i]
			if t.Mid() == "" && t.kind == remoteKind && possibleDirection == t.Direction() {
				return t, append(localTransceivers[:i], localTransceivers[i+1:]...)
			}
		}
	}

	return nil, localTransceivers
}

// handleUnknownRTPPacket consumes a single RTP Packet and returns information that is helpful
// for demuxing and handling an unknown SSRC (usually for Simulcast)
func handleUnknownRTPPacket(buf []byte, midExtensionID, streamIDExtensionID, repairStreamIDExtensionID uint8, mid, rid, rsid *string) (payloadType PayloadType, err error) {
	rp := &rtp.Packet{}
	if err = rp.Unmarshal(buf); err != nil {
		return
	}

	if !rp.Header.Extension {
		return
	}

	payloadType = PayloadType(rp.PayloadType)
	if payload := rp.GetExtension(midExtensionID); payload != nil {
		*mid = string(payload)
	}

	if payload := rp.GetExtension(streamIDExtensionID); payload != nil {
		*rid = string(payload)
	}

	if payload := rp.GetExtension(repairStreamIDExtensionID); payload != nil {
		*rsid = string(payload)
	}

	return
}
