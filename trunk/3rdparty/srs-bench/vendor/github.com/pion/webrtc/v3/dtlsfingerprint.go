// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package webrtc

// DTLSFingerprint specifies the hash function algorithm and certificate
// fingerprint as described in https://tools.ietf.org/html/rfc4572.
type DTLSFingerprint struct {
	// Algorithm specifies one of the the hash function algorithms defined in
	// the 'Hash function Textual Names' registry.
	Algorithm string `json:"algorithm"`

	// Value specifies the value of the certificate fingerprint in lowercase
	// hex string as expressed utilizing the syntax of 'fingerprint' in
	// https://tools.ietf.org/html/rfc4572#section-5.
	Value string `json:"value"`
}
