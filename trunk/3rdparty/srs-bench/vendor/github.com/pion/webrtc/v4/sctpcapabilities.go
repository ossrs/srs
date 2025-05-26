// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// SCTPCapabilities indicates the capabilities of the SCTPTransport.
type SCTPCapabilities struct {
	MaxMessageSize uint32 `json:"maxMessageSize"`
}
