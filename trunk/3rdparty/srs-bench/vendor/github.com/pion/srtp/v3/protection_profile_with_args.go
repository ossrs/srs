// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

// protectionProfileWithArgs is a wrapper around ProtectionProfile that allows to
// specify additional arguments for the profile.
type protectionProfileWithArgs struct {
	ProtectionProfile
	authTagRTPLen *int
}

// AuthTagRTPLen returns length of RTP authentication tag in bytes for AES protection profiles.
// For AEAD ones it returns zero.
func (p protectionProfileWithArgs) AuthTagRTPLen() (int, error) {
	if p.authTagRTPLen != nil {
		return *p.authTagRTPLen, nil
	}

	return p.ProtectionProfile.AuthTagRTPLen()
}
