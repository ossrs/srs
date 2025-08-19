// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// DTLSParameters holds information relating to DTLS configuration.
type DTLSParameters struct {
	Role         DTLSRole          `json:"role"`
	Fingerprints []DTLSFingerprint `json:"fingerprints"`
}
