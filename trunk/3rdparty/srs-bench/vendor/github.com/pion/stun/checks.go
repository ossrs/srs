// +build !debug

package stun

import "github.com/pion/stun/internal/hmac"

// CheckSize returns ErrAttrSizeInvalid if got is not equal to expected.
func CheckSize(_ AttrType, got, expected int) error {
	if got == expected {
		return nil
	}
	return ErrAttributeSizeInvalid
}

func checkHMAC(got, expected []byte) error {
	if hmac.Equal(got, expected) {
		return nil
	}
	return ErrIntegrityMismatch
}

func checkFingerprint(got, expected uint32) error {
	if got == expected {
		return nil
	}
	return ErrFingerprintMismatch
}

// IsAttrSizeInvalid returns true if error means that attribute size is invalid.
func IsAttrSizeInvalid(err error) bool {
	return err == ErrAttributeSizeInvalid
}

// CheckOverflow returns ErrAttributeSizeOverflow if got is bigger that max.
func CheckOverflow(_ AttrType, got, max int) error {
	if got <= max {
		return nil
	}
	return ErrAttributeSizeOverflow
}

// IsAttrSizeOverflow returns true if error means that attribute size is too big.
func IsAttrSizeOverflow(err error) bool {
	return err == ErrAttributeSizeOverflow
}
