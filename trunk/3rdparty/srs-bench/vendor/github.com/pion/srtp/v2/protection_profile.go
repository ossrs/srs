package srtp

import "fmt"

// ProtectionProfile specifies Cipher and AuthTag details, similar to TLS cipher suite
type ProtectionProfile uint16

// Supported protection profiles
const (
	ProtectionProfileAes128CmHmacSha1_80 ProtectionProfile = 0x0001
	ProtectionProfileAeadAes128Gcm       ProtectionProfile = 0x0007
)

func (p ProtectionProfile) keyLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_80:
		fallthrough
	case ProtectionProfileAeadAes128Gcm:
		return 16, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) saltLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_80:
		return 14, nil
	case ProtectionProfileAeadAes128Gcm:
		return 12, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) authTagLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_80:
		return (&srtpCipherAesCmHmacSha1{}).authTagLen(), nil
	case ProtectionProfileAeadAes128Gcm:
		return (&srtpCipherAeadAesGcm{}).authTagLen(), nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}

func (p ProtectionProfile) authKeyLen() (int, error) {
	switch p {
	case ProtectionProfileAes128CmHmacSha1_80:
		return 20, nil
	case ProtectionProfileAeadAes128Gcm:
		return 0, nil
	default:
		return 0, fmt.Errorf("%w: %#v", errNoSuchSRTPProfile, p)
	}
}
