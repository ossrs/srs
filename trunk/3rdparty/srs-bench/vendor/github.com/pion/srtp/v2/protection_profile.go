// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package srtp

import "fmt"

// ProtectionProfile specifies Cipher and AuthTag details, similar to TLS cipher suite
type ProtectionProfile uint16

// Supported protection profiles
// See https://www.iana.org/assignments/srtp-protection/srtp-protection.xhtml
const (
	ProtectionProfileAes128CmHmacSha1_80 ProtectionProfile = 0x0001
	ProtectionProfileAes128CmHmacSha1_32 ProtectionProfile = 0x0002
	ProtectionProfileAeadAes128Gcm       ProtectionProfile = 0x0007
	ProtectionProfileAeadAes256Gcm       ProtectionProfile = 0x0008
)

func (p ProtectionProfile) keyLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80, ProtectionProfileAeadAes128Gcm:
		return 16, nil
	case ProtectionProfileAeadAes256Gcm:
		return 32, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) saltLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80:
		return 14, nil
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return 12, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) rtpAuthTagLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_80:
		return 10, nil
	case ProtectionProfileAes128CmHmacSha1_32:
		return 4, nil
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return 0, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) rtcpAuthTagLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80:
		return 10, nil
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return 0, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) aeadAuthTagLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80:
		return 0, nil
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return 16, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) authKeyLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_32, ProtectionProfileAes128CmHmacSha1_80:
		return 20, nil
	case ProtectionProfileAeadAes128Gcm, ProtectionProfileAeadAes256Gcm:
		return 0, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}
