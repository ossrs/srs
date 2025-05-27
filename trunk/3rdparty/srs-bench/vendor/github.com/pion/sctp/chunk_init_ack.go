// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp // nolint:dupl

import (
	"errors"
	"fmt"
)

/*
chunkInitAck represents an SCTP Chunk of type INIT ACK

See chunkInitCommon for the fixed headers

	Variable Parameters                  Status     Type Value
	-------------------------------------------------------------
	State Cookie                        Mandatory   7
	IPv4 IP (Note 1)               		Optional    5
	IPv6 IP (Note 1)               		Optional    6
	Unrecognized Parameter              Optional    8
	Reserved for ECN Capable (Note 2)   Optional    32768 (0x8000)
	Host Name IP (Note 3)          		Optional    11<Paste>
*/
type chunkInitAck struct {
	chunkHeader
	chunkInitCommon
}

// Init ack chunk errors.
var (
	ErrChunkTypeNotInitAck              = errors.New("ChunkType is not of type INIT ACK")
	ErrChunkNotLongEnoughForParams      = errors.New("chunk Value isn't long enough for mandatory parameters exp")
	ErrChunkTypeInitAckFlagZero         = errors.New("ChunkType of type INIT ACK flags must be all 0")
	ErrInitAckUnmarshalFailed           = errors.New("failed to unmarshal INIT body")
	ErrInitCommonDataMarshalFailed      = errors.New("failed marshaling INIT common data")
	ErrChunkTypeInitAckInitateTagZero   = errors.New("ChunkType of type INIT ACK InitiateTag must not be 0")
	ErrInitAckInboundStreamRequestZero  = errors.New("INIT ACK inbound stream request must be > 0")
	ErrInitAckOutboundStreamRequestZero = errors.New("INIT ACK outbound stream request must be > 0")
	ErrInitAckAdvertisedReceiver1500    = errors.New("INIT ACK Advertised Receiver Window Credit (a_rwnd) must be >= 1500")
)

func (i *chunkInitAck) unmarshal(raw []byte) error {
	if err := i.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if i.typ != ctInitAck {
		return fmt.Errorf("%w: actually is %s", ErrChunkTypeNotInitAck, i.typ.String())
	} else if len(i.raw) < initChunkMinLength {
		return fmt.Errorf("%w: %d actual: %d", ErrChunkNotLongEnoughForParams, initChunkMinLength, len(i.raw))
	}

	// The Chunk Flags field in INIT is reserved, and all bits in it should
	// be set to 0 by the sender and ignored by the receiver.  The sequence
	// of parameters within an INIT can be processed in any order.
	if i.flags != 0 {
		return ErrChunkTypeInitAckFlagZero
	}

	if err := i.chunkInitCommon.unmarshal(i.raw); err != nil {
		return fmt.Errorf("%w: %v", ErrInitAckUnmarshalFailed, err) //nolint:errorlint
	}

	return nil
}

func (i *chunkInitAck) marshal() ([]byte, error) {
	initShared, err := i.chunkInitCommon.marshal()
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrInitCommonDataMarshalFailed, err) //nolint:errorlint
	}

	i.chunkHeader.typ = ctInitAck
	i.chunkHeader.raw = initShared

	return i.chunkHeader.marshal()
}

func (i *chunkInitAck) check() (abort bool, err error) {
	// The receiver of the INIT ACK records the value of the Initiate Tag
	// parameter.  This value MUST be placed into the Verification Tag
	// field of every SCTP packet that the INIT ACK receiver transmits
	// within this association.
	//
	// The Initiate Tag MUST NOT take the value 0.  See Section 5.3.1 for
	// more on the selection of the Initiate Tag value.
	//
	// If the value of the Initiate Tag in a received INIT ACK chunk is
	// found to be 0, the receiver MUST destroy the association
	// discarding its TCB.  The receiver MAY send an ABORT for debugging
	// purpose.
	if i.initiateTag == 0 {
		abort = true

		return abort, ErrChunkTypeInitAckInitateTagZero
	}

	// Defines the maximum number of streams the sender of this INIT ACK
	// chunk allows the peer end to create in this association.  The
	// value 0 MUST NOT be used.
	//
	// Note: There is no negotiation of the actual number of streams but
	// instead the two endpoints will use the min(requested, offered).
	// See Section 5.1.1 for details.
	//
	// Note: A receiver of an INIT ACK with the MIS value set to 0 SHOULD
	// destroy the association discarding its TCB.
	if i.numInboundStreams == 0 {
		abort = true

		return abort, ErrInitAckInboundStreamRequestZero
	}

	// Defines the number of outbound streams the sender of this INIT ACK
	// chunk wishes to create in this association.  The value of 0 MUST
	// NOT be used, and the value MUST NOT be greater than the MIS value
	// sent in the INIT chunk.
	//
	// Note: A receiver of an INIT ACK with the OS value set to 0 SHOULD
	// destroy the association discarding its TCB.

	if i.numOutboundStreams == 0 {
		abort = true

		return abort, ErrInitAckOutboundStreamRequestZero
	}

	// An SCTP receiver MUST be able to receive a minimum of 1500 bytes in
	// one SCTP packet.  This means that an SCTP endpoint MUST NOT indicate
	// less than 1500 bytes in its initial a_rwnd sent in the INIT or INIT
	// ACK.
	if i.advertisedReceiverWindowCredit < 1500 {
		abort = true

		return abort, ErrInitAckAdvertisedReceiver1500
	}

	return false, nil
}

// String makes chunkInitAck printable.
func (i *chunkInitAck) String() string {
	return fmt.Sprintf("%s\n%s", i.chunkHeader, i.chunkInitCommon)
}
