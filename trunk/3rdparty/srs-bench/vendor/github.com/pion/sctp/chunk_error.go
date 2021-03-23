package sctp // nolint:dupl

import (
	"fmt"

	"github.com/pkg/errors"
)

/*
   Operation Error (ERROR) (9)

   An endpoint sends this chunk to its peer endpoint to notify it of
   certain error conditions.  It contains one or more error causes.  An
   Operation Error is not considered fatal in and of itself, but may be
   used with an ERROR chunk to report a fatal condition.  It has the
   following parameters:

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |   Type = 9    | Chunk  Flags  |           Length              |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       \                                                               \
       /                    one or more Error Causes                   /
       \                                                               \
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+

   Chunk Flags: 8 bits

      Set to 0 on transmit and ignored on receipt.

   Length: 16 bits (unsigned integer)

      Set to the size of the chunk in bytes, including the chunk header
      and all the Error Cause fields present.
*/
type chunkError struct {
	chunkHeader
	errorCauses []errorCause
}

func (a *chunkError) unmarshal(raw []byte) error {
	if err := a.chunkHeader.unmarshal(raw); err != nil {
		return err
	}

	if a.typ != ctError {
		return errors.Errorf("ChunkType is not of type ctError, actually is %s", a.typ.String())
	}

	offset := chunkHeaderSize
	for {
		if len(raw)-offset < 4 {
			break
		}

		e, err := buildErrorCause(raw[offset:])
		if err != nil {
			return errors.Wrap(err, "Failed build Error Chunk")
		}

		offset += int(e.length())
		a.errorCauses = append(a.errorCauses, e)
	}
	return nil
}

func (a *chunkError) marshal() ([]byte, error) {
	a.chunkHeader.typ = ctError
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

func (a *chunkError) check() (abort bool, err error) {
	return false, nil
}

// String makes chunkError printable
func (a *chunkError) String() string {
	res := a.chunkHeader.String()

	for _, cause := range a.errorCauses {
		res += fmt.Sprintf("\n - %s", cause)
	}

	return res
}
