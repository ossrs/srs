// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package extension

// SRTPProtectionProfile defines the parameters and options that are in effect for the SRTP processing
// https://tools.ietf.org/html/rfc5764#section-4.1.2
type SRTPProtectionProfile uint16

const (
	SRTP_AES128_CM_HMAC_SHA1_80 SRTPProtectionProfile = 0x0001 // nolint
	SRTP_AES128_CM_HMAC_SHA1_32 SRTPProtectionProfile = 0x0002 // nolint
	SRTP_AEAD_AES_128_GCM       SRTPProtectionProfile = 0x0007 // nolint
	SRTP_AEAD_AES_256_GCM       SRTPProtectionProfile = 0x0008 // nolint
)

func srtpProtectionProfiles() map[SRTPProtectionProfile]bool {
	return map[SRTPProtectionProfile]bool{
		SRTP_AES128_CM_HMAC_SHA1_80: true,
		SRTP_AES128_CM_HMAC_SHA1_32: true,
		SRTP_AEAD_AES_128_GCM:       true,
		SRTP_AEAD_AES_256_GCM:       true,
	}
}
