// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js && wasm
// +build js,wasm

package webrtc

import "syscall/js"

// SCTPTransport provides details about the SCTP transport.
type SCTPTransport struct {
	// Pointer to the underlying JavaScript SCTPTransport object.
	underlying js.Value
}

// Transport returns the DTLSTransport instance the SCTPTransport is sending over.
func (r *SCTPTransport) Transport() *DTLSTransport {
	underlying := r.underlying.Get("transport")
	if underlying.IsNull() || underlying.IsUndefined() {
		return nil
	}

	return &DTLSTransport{
		underlying: underlying,
	}
}
