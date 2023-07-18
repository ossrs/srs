// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

//go:build js && wasm
// +build js,wasm

package webrtc

import "syscall/js"

// DTLSTransport allows an application access to information about the DTLS
// transport over which RTP and RTCP packets are sent and received by
// RTPSender and RTPReceiver, as well other data such as SCTP packets sent
// and received by data channels.
type DTLSTransport struct {
	// Pointer to the underlying JavaScript DTLSTransport object.
	underlying js.Value
}

// ICETransport returns the currently-configured *ICETransport or nil
// if one has not been configured
func (r *DTLSTransport) ICETransport() *ICETransport {
	underlying := r.underlying.Get("iceTransport")
	if underlying.IsNull() || underlying.IsUndefined() {
		return nil
	}

	return &ICETransport{
		underlying: underlying,
	}
}
