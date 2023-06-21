package sctp

import (
	"fmt"
)

/*
This error cause MAY be included in ABORT chunks that are sent
because of an upper-layer request.  The upper layer can specify an
Upper Layer Abort Reason that is transported by SCTP transparently
and MAY be delivered to the upper-layer protocol at the peer.

	 0                   1                   2                   3
	 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	|         Cause Code=12         |      Cause Length=Variable    |
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
	/                    Upper Layer Abort Reason                   /
	\                                                               \
	+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
*/
type errorCauseUserInitiatedAbort struct {
	errorCauseHeader
	upperLayerAbortReason []byte
}

func (e *errorCauseUserInitiatedAbort) marshal() ([]byte, error) {
	e.code = userInitiatedAbort
	e.errorCauseHeader.raw = e.upperLayerAbortReason
	return e.errorCauseHeader.marshal()
}

func (e *errorCauseUserInitiatedAbort) unmarshal(raw []byte) error {
	err := e.errorCauseHeader.unmarshal(raw)
	if err != nil {
		return err
	}

	e.upperLayerAbortReason = e.errorCauseHeader.raw
	return nil
}

// String makes errorCauseUserInitiatedAbort printable
func (e *errorCauseUserInitiatedAbort) String() string {
	return fmt.Sprintf("%s: %s", e.errorCauseHeader.String(), e.upperLayerAbortReason)
}
