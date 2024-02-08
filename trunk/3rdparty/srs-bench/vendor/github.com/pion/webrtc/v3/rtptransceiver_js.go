// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js && wasm
// +build js,wasm

package webrtc

import (
	"syscall/js"
)

// RTPTransceiver represents a combination of an RTPSender and an RTPReceiver that share a common mid.
type RTPTransceiver struct {
	// Pointer to the underlying JavaScript RTCRTPTransceiver object.
	underlying js.Value
}

// Direction returns the RTPTransceiver's current direction
func (r *RTPTransceiver) Direction() RTPTransceiverDirection {
	return NewRTPTransceiverDirection(r.underlying.Get("direction").String())
}

// Sender returns the RTPTransceiver's RTPSender if it has one
func (r *RTPTransceiver) Sender() *RTPSender {
	underlying := r.underlying.Get("sender")
	if underlying.IsNull() {
		return nil
	}

	return &RTPSender{underlying: underlying}
}

// Receiver returns the RTPTransceiver's RTPReceiver if it has one
func (r *RTPTransceiver) Receiver() *RTPReceiver {
	underlying := r.underlying.Get("receiver")
	if underlying.IsNull() {
		return nil
	}

	return &RTPReceiver{underlying: underlying}
}
