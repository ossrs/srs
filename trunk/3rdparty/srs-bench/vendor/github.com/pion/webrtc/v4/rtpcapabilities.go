// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// RTPCapabilities represents the capabilities of a transceiver
//
// https://w3c.github.io/webrtc-pc/#rtcrtpcapabilities
type RTPCapabilities struct {
	Codecs           []RTPCodecCapability
	HeaderExtensions []RTPHeaderExtensionCapability
}
