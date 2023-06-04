// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js && wasm
// +build js,wasm

package webrtc

import "syscall/js"

// ICETransport allows an application access to information about the ICE
// transport over which packets are sent and received.
type ICETransport struct {
	// Pointer to the underlying JavaScript ICETransport object.
	underlying js.Value
}

// GetSelectedCandidatePair returns the selected candidate pair on which packets are sent
// if there is no selected pair nil is returned
func (t *ICETransport) GetSelectedCandidatePair() (*ICECandidatePair, error) {
	val := t.underlying.Call("getSelectedCandidatePair")
	if val.IsNull() || val.IsUndefined() {
		return nil, nil
	}

	return NewICECandidatePair(
		valueToICECandidate(val.Get("local")),
		valueToICECandidate(val.Get("remote")),
	), nil
}
