// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build !js
// +build !js

package webrtc

import "github.com/pion/interceptor"

// SetRTPParameters applies provided RTPParameters the RTPReceiver's tracks.
//
// This method is part of the ORTC API. It is not
// meant to be used together with the basic WebRTC API.
//
// The amount of provided codecs must match the number of tracks on the receiver.
func (r *RTPReceiver) SetRTPParameters(params RTPParameters) {
	headerExtensions := make([]interceptor.RTPHeaderExtension, 0, len(params.HeaderExtensions))
	for _, h := range params.HeaderExtensions {
		headerExtensions = append(headerExtensions, interceptor.RTPHeaderExtension{ID: h.ID, URI: h.URI})
	}

	r.mu.Lock()
	defer r.mu.Unlock()

	for ndx, codec := range params.Codecs {
		currentTrack := r.tracks[ndx].track

		r.tracks[ndx].streamInfo.RTPHeaderExtensions = headerExtensions

		currentTrack.mu.Lock()
		currentTrack.codec = codec
		currentTrack.params = params
		currentTrack.mu.Unlock()
	}
}
