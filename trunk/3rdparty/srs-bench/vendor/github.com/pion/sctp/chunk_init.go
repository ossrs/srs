// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package sctp // nolint:dupl

import (
	"errors"
	"fmt"
)

/*
Init represents an SCTP Chunk of type INIT

See chunkInitCommon for the fixed headers

	Variable Parameters               	Status     	Type Value
	-------------------------------------------------------------
	IPv4 IP (Note 1)               		Optional    5
	IPv6 IP (Note 1)               		Optional    6
	Cookie Preservative                 Optional    9
	Reserved for ECN Capable (Note 2)   Optional    32768 (0x8000)
	Host Name IP (Note 3)          		Optional    11
	Supported IP Types (Note 4)    		Optional    12
*/
type chunkInit struct {
	chunkHeader
	chunkInitCommon
}

// Init chunk errors.
var (
	ErrChunkTypeNotTypeInit          = errors.New("ChunkType is not of type INIT")
	ErrChunkValueNotLongEnough       = errors.New("chunk Value isn't long enough for mandatory parameters exp")
	ErrChunkTypeInitFlagZero         = errors.New("ChunkType of type INIT flags must be all 0")
	ErrChunkTypeInitUnmarshalFailed  = errors.New("failed to unmarshal INIT body")
	ErrChunkTypeInitMarshalFailed    = errors.New("failed marshaling INIT common data")
	ErrChunkTypeInitInitateTagZero   = errors.New("ChunkType of type INIT ACK InitiateTag must not be 0")
	ErrInitInboundStreamRequestZero  = errors.New("INIT ACK inbound stream request must be > 0")
	ErrInitOutboundStreamRequestZero = errors.New("INIT ACK outbound stream request must be > 0")
	ErrInitAdvertisedReceiver1500    = errors.New("INIT ACK Advertised Receiver Window Credit (a_rwnd) must be >= 1500")
	ErrInitUnknownParam              = errors.New("INIT with unknown param")
)

func (i *chunkInit) unmarshal(raw []byte) error {
	if err := i.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if i.typ != ctInit {
		return fmt.Errorf("%w: actually is %s", ErrChunkTypeNotTypeInit, i.typ.String())
	} else if len(i.raw) < initChunkMinLength {
		return fmt.Errorf("%w: %d actual: %d", ErrChunkValueNotLongEnough, initChunkMinLength, len(i.raw))
	}

	// The Chunk Flags field in INIT is reserved, and all bits in it should
	// be set to 0 by the sender and ignored by the receiver.  The sequence
	// of parameters within an INIT can be processed in any order.
	if i.flags != 0 {
		return ErrChunkTypeInitFlagZero
	}

	if err := i.chunkInitCommon.unmarshal(i.raw); err != nil {
		return fmt.Errorf("%w: %v", ErrChunkTypeInitUnmarshalFailed, err) //nolint:errorlint
	}

	return nil
}

func (i *chunkInit) marshal() ([]byte, error) {
	initShared, err := i.chunkInitCommon.marshal()
	if err != nil {
		return nil, fmt.Errorf("%w: %v", ErrChunkTypeInitMarshalFailed, err) //nolint:errorlint
	}

	i.chunkHeader.typ = ctInit
	i.chunkHeader.raw = initShared

	return i.chunkHeader.marshal()
}

func (i *chunkInit) check() (abort bool, err error) {
	// The receiver of the INIT (the responding end) records the value of
	// the Initiate Tag parameter.  This value MUST be placed into the
	// Verification Tag field of every SCTP packet that the receiver of
	// the INIT transmits within this association.
	//
	// The Initiate Tag is allowed to have any value except 0.  See
	// Section 5.3.1 for more on the selection of the tag value.
	//
	// If the value of the Initiate Tag in a received INIT chunk is found
	// to be 0, the receiver MUST treat it as an error and close the
	// association by transmitting an ABORT.
	if i.initiateTag == 0 {
		return true, ErrChunkTypeInitInitateTagZero
	}

	// Defines the maximum number of streams the sender of this INIT
	// chunk allows the peer end to create in this association.  The
	// value 0 MUST NOT be used.
	//
	// Note: There is no negotiation of the actual number of streams but
	// instead the two endpoints will use the min(requested, offered).
	// See Section 5.1.1 for details.
	//
	// Note: A receiver of an INIT with the MIS value of 0 SHOULD abort
	// the association.
	if i.numInboundStreams == 0 {
		return true, ErrInitInboundStreamRequestZero
	}

	// Defines the number of outbound streams the sender of this INIT
	// chunk wishes to create in this association.  The value of 0 MUST
	// NOT be used.
	//
	// Note: A receiver of an INIT with the OS value set to 0 SHOULD
	// abort the association.

	if i.numOutboundStreams == 0 {
		return true, ErrInitOutboundStreamRequestZero
	}

	// An SCTP receiver MUST be able to receive a minimum of 1500 bytes in
	// one SCTP packet.  This means that an SCTP endpoint MUST NOT indicate
	// less than 1500 bytes in its initial a_rwnd sent in the INIT or INIT
	// ACK.
	if i.advertisedReceiverWindowCredit < 1500 {
		return true, ErrInitAdvertisedReceiver1500
	}

	for _, p := range i.unrecognizedParams {
		if p.unrecognizedAction == paramHeaderUnrecognizedActionStop ||
			p.unrecognizedAction == paramHeaderUnrecognizedActionStopAndReport {
			return true, ErrInitUnknownParam
		}
	}

	return false, nil
}

// String makes chunkInit printable.
func (i *chunkInit) String() string {
	return fmt.Sprintf("%s\n%s", i.chunkHeader, i.chunkInitCommon)
}
