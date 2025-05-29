// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

// Package ciphersuite provides the crypto operations needed for a DTLS CipherSuite
package ciphersuite

import (
	"encoding/binary"
	"errors"

	"github.com/pion/dtls/v3/internal/util"
	"github.com/pion/dtls/v3/pkg/protocol"
	"github.com/pion/dtls/v3/pkg/protocol/recordlayer"
	"golang.org/x/crypto/cryptobyte"
)

const (
	// 8 bytes of 0xff.
	// https://datatracker.ietf.org/doc/html/rfc9146#name-record-payload-protection
	seqNumPlaceholder = 0xffffffffffffffff
)

var (
	//nolint:goerr113
	errNotEnoughRoomForNonce = &protocol.InternalError{Err: errors.New("buffer not long enough to contain nonce")}
	//nolint:goerr113
	errDecryptPacket = &protocol.TemporaryError{Err: errors.New("failed to decrypt packet")}
	//nolint:goerr113
	errInvalidMAC = &protocol.TemporaryError{Err: errors.New("invalid mac")}
	//nolint:goerr113
	errFailedToCast = &protocol.FatalError{Err: errors.New("failed to cast")}
)

func generateAEADAdditionalData(h *recordlayer.Header, payloadLen int) []byte {
	var additionalData [13]byte

	// SequenceNumber MUST be set first
	// we only want uint48, clobbering an extra 2 (using uint64, Golang doesn't have uint48)
	binary.BigEndian.PutUint64(additionalData[:], h.SequenceNumber)
	binary.BigEndian.PutUint16(additionalData[:], h.Epoch)
	additionalData[8] = byte(h.ContentType)
	additionalData[9] = h.Version.Major
	additionalData[10] = h.Version.Minor
	//nolint:gosec //G115
	binary.BigEndian.PutUint16(additionalData[len(additionalData)-2:], uint16(payloadLen))

	return additionalData[:]
}

// generateAEADAdditionalDataCID generates additional data for AEAD ciphers
// according to https://datatracker.ietf.org/doc/html/rfc9146#name-aead-ciphers
func generateAEADAdditionalDataCID(h *recordlayer.Header, payloadLen int) []byte {
	var builder cryptobyte.Builder

	builder.AddUint64(seqNumPlaceholder)
	builder.AddUint8(uint8(protocol.ContentTypeConnectionID))
	builder.AddUint8(uint8(len(h.ConnectionID))) //nolint:gosec //G115
	builder.AddUint8(uint8(protocol.ContentTypeConnectionID))
	builder.AddUint8(h.Version.Major)
	builder.AddUint8(h.Version.Minor)
	builder.AddUint16(h.Epoch)
	util.AddUint48(&builder, h.SequenceNumber)
	builder.AddBytes(h.ConnectionID)
	builder.AddUint16(uint16(payloadLen)) //nolint:gosec //G115

	return builder.BytesOrPanic()
}

// examinePadding returns, in constant time, the length of the padding to remove
// from the end of payload. It also returns a byte which is equal to 255 if the
// padding was valid and 0 otherwise. See RFC 2246, Section 6.2.3.2.
//
// https://github.com/golang/go/blob/039c2081d1178f90a8fa2f4e6958693129f8de33/src/crypto/tls/conn.go#L245
func examinePadding(payload []byte) (toRemove int, good byte) {
	if len(payload) < 1 {
		return 0, 0
	}

	paddingLen := payload[len(payload)-1]
	t := uint(len(payload)-1) - uint(paddingLen) //nolint:gosec //G115
	// if len(payload) >= (paddingLen - 1) then the MSB of t is zero
	good = byte(int32(^t) >> 31) //nolint:gosec //G115

	// The maximum possible padding length plus the actual length field
	toCheck := 256
	// The length of the padded data is public, so we can use an if here
	if toCheck > len(payload) {
		toCheck = len(payload)
	}

	for i := 0; i < toCheck; i++ {
		t := uint(paddingLen) - uint(i) //nolint:gosec //G115
		// if i <= paddingLen then the MSB of t is zero
		mask := byte(int32(^t) >> 31) //nolint:gosec //G115
		b := payload[len(payload)-1-i]
		good &^= mask&paddingLen ^ mask&b
	}

	// We AND together the bits of good and replicate the result across
	// all the bits.
	good &= good << 4
	good &= good << 2
	good &= good << 1
	good = uint8(int8(good) >> 7) //nolint:gosec //G115

	toRemove = int(paddingLen) + 1

	return toRemove, good
}
