package sctp // nolint:dupl

import (
	"fmt"

	"github.com/pkg/errors"
)

/*
Abort represents an SCTP Chunk of type ABORT

The ABORT chunk is sent to the peer of an association to close the
association.  The ABORT chunk may contain Cause Parameters to inform
the receiver about the reason of the abort.  DATA chunks MUST NOT be
bundled with ABORT.  Control chunks (except for INIT, INIT ACK, and
SHUTDOWN COMPLETE) MAY be bundled with an ABORT, but they MUST be
placed before the ABORT in the SCTP packet or they will be ignored by
the receiver.

 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   Type = 6    |Reserved     |T|           Length              |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                   zero or more Error Causes                   |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type chunkAbort struct {
	chunkHeader
	errorCauses []errorCause
}

func (a *chunkAbort) unmarshal(raw []byte) error {
	if err := a.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if a.typ != ctAbort {
		return errors.Errorf("ChunkType is not of type ABORT, actually is %s", a.typ.String())
	}

	offset := chunkHeaderSize
	for {
		if len(raw)-offset < 4 {
			break
		}

		e, err := buildErrorCause(raw[offset:])
		if err != nil {
			return errors.Wrap(err, "Failed build Abort Chunk")
		}

		offset += int(e.length())
		a.errorCauses = append(a.errorCauses, e)
	}
	return nil
}

func (a *chunkAbort) marshal() ([]byte, error) {
	a.chunkHeader.typ = ctAbort
	a.flags = 0x00
	a.raw = []byte{}
	for _, ec := range a.errorCauses {
		raw, err := ec.marshal()
		if err != nil {
			return nil, err
		}
		a.raw = append(a.raw, raw...)
	}
	return a.chunkHeader.marshal()
}

func (a *chunkAbort) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkAbort printable
func (a *chunkAbort) String() string {
	res := a.chunkHeader.String()

	for _, cause := range a.errorCauses {
		res += fmt.Sprintf("\n - %s", cause)
	}

	return res
}
