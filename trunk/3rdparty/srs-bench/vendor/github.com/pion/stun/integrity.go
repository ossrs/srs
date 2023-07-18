// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package stun

import ( //nolint:gci
	"crypto/md5"  //nolint:gosec
	"crypto/sha1" //nolint:gosec
	"errors"
	"fmt"
	"strings"

	"github.com/pion/stun/internal/hmac"
)

// separator for credentials.
const credentialsSep = ":"

// NewLongTermIntegrity returns new MessageIntegrity with key for long-term
// credentials. Password, username, and realm must be SASL-prepared.
func NewLongTermIntegrity(username, realm, password string) MessageIntegrity {
	k := strings.Join([]string{username, realm, password}, credentialsSep)
	h := md5.New() //nolint:gosec
	fmt.Fprint(h, k)
	return MessageIntegrity(h.Sum(nil))
}

// NewShortTermIntegrity returns new MessageIntegrity with key for short-term
// credentials. Password must be SASL-prepared.
func NewShortTermIntegrity(password string) MessageIntegrity {
	return MessageIntegrity(password)
}

// MessageIntegrity represents MESSAGE-INTEGRITY attribute.
//
// AddTo and Check methods are using zero-allocation version of hmac, see
// newHMAC function and internal/hmac/pool.go.
//
// RFC 5389 Section 15.4
type MessageIntegrity []byte

func newHMAC(key, message, buf []byte) []byte {
	mac := hmac.AcquireSHA1(key)
	writeOrPanic(mac, message)
	defer hmac.PutSHA1(mac)
	return mac.Sum(buf)
}

func (i MessageIntegrity) String() string {
	return fmt.Sprintf("KEY: 0x%x", []byte(i))
}

const messageIntegritySize = 20

// ErrFingerprintBeforeIntegrity means that FINGERPRINT attribute is already in
// message, so MESSAGE-INTEGRITY attribute cannot be added.
var ErrFingerprintBeforeIntegrity = errors.New("FINGERPRINT before MESSAGE-INTEGRITY attribute")

// AddTo adds MESSAGE-INTEGRITY attribute to message.
//
// CPU costly, see BenchmarkMessageIntegrity_AddTo.
func (i MessageIntegrity) AddTo(m *Message) error {
	for _, a := range m.Attributes {
		// Message should not contain FINGERPRINT attribute
		// before MESSAGE-INTEGRITY.
		if a.Type == AttrFingerprint {
			return ErrFingerprintBeforeIntegrity
		}
	}
	// The text used as input to HMAC is the STUN message,
	// including the header, up to and including the attribute preceding the
	// MESSAGE-INTEGRITY attribute.
	length := m.Length
	// Adjusting m.Length to contain MESSAGE-INTEGRITY TLV.
	m.Length += messageIntegritySize + attributeHeaderSize
	m.WriteLength()                            // writing length to m.Raw
	v := newHMAC(i, m.Raw, m.Raw[len(m.Raw):]) // calculating HMAC for adjusted m.Raw
	m.Length = length                          // changing m.Length back

	// Copy hmac value to temporary variable to protect it from resetting
	// while processing m.Add call.
	vBuf := make([]byte, sha1.Size)
	copy(vBuf, v)

	m.Add(AttrMessageIntegrity, vBuf)
	return nil
}

// ErrIntegrityMismatch means that computed HMAC differs from expected.
var ErrIntegrityMismatch = errors.New("integrity check failed")

// Check checks MESSAGE-INTEGRITY attribute.
//
// CPU costly, see BenchmarkMessageIntegrity_Check.
func (i MessageIntegrity) Check(m *Message) error {
	v, err := m.Get(AttrMessageIntegrity)
	if err != nil {
		return err
	}

	// Adjusting length in header to match m.Raw that was
	// used when computing HMAC.
	var (
		length         = m.Length
		afterIntegrity = false
		sizeReduced    int
	)
	for _, a := range m.Attributes {
		if afterIntegrity {
			sizeReduced += nearestPaddedValueLength(int(a.Length))
			sizeReduced += attributeHeaderSize
		}
		if a.Type == AttrMessageIntegrity {
			afterIntegrity = true
		}
	}
	m.Length -= uint32(sizeReduced)
	m.WriteLength()
	// startOfHMAC should be first byte of integrity attribute.
	startOfHMAC := messageHeaderSize + m.Length - (attributeHeaderSize + messageIntegritySize)
	b := m.Raw[:startOfHMAC] // data before integrity attribute
	expected := newHMAC(i, b, m.Raw[len(m.Raw):])
	m.Length = length
	m.WriteLength() // writing length back
	return checkHMAC(v, expected)
}
