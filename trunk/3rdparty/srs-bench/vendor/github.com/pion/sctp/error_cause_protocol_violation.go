package sctp

import (
	"fmt"

	"github.com/pkg/errors"
)

/*
   This error cause MAY be included in ABORT chunks that are sent
   because an SCTP endpoint detects a protocol violation of the peer
   that is not covered by the error causes described in Section 3.3.10.1
   to Section 3.3.10.12.  An implementation MAY provide additional
   information specifying what kind of protocol violation has been
   detected.

        0                   1                   2                   3
        0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       |         Cause Code=13         |      Cause Length=Variable    |
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
       /                    Additional Information                     /
       \                                                               \
       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type errorCauseProtocolViolation struct {
	errorCauseHeader
	additionalInformation []byte
}

func (e *errorCauseProtocolViolation) marshal() ([]byte, error) {
	e.raw = e.additionalInformation
	return e.errorCauseHeader.marshal()
}

func (e *errorCauseProtocolViolation) unmarshal(raw []byte) error {
	err := e.errorCauseHeader.unmarshal(raw)
	if err != nil {
		return errors.Wrap(err, "Unable to unmarshal Protocol Violation error")
	}

	e.additionalInformation = e.raw

	return nil
}

// String makes errorCauseProtocolViolation printable
func (e *errorCauseProtocolViolation) String() string {
	return fmt.Sprintf("%s: %s", e.errorCauseHeader, e.additionalInformation)
}
