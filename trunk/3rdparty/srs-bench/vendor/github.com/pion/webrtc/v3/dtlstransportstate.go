package webrtc

// DTLSTransportState indicates the DTLS transport establishment state.
type DTLSTransportState int

const (
	// DTLSTransportStateNew indicates that DTLS has not started negotiating
	// yet.
	DTLSTransportStateNew DTLSTransportState = iota + 1

	// DTLSTransportStateConnecting indicates that DTLS is in the process of
	// negotiating a secure connection and verifying the remote fingerprint.
	DTLSTransportStateConnecting

	// DTLSTransportStateConnected indicates that DTLS has completed
	// negotiation of a secure connection and verified the remote fingerprint.
	DTLSTransportStateConnected

	// DTLSTransportStateClosed indicates that the transport has been closed
	// intentionally as the result of receipt of a close_notify alert, or
	// calling close().
	DTLSTransportStateClosed

	// DTLSTransportStateFailed indicates that the transport has failed as
	// the result of an error (such as receipt of an error alert or failure to
	// validate the remote fingerprint).
	DTLSTransportStateFailed
)

// This is done this way because of a linter.
const (
	dtlsTransportStateNewStr        = "new"
	dtlsTransportStateConnectingStr = "connecting"
	dtlsTransportStateConnectedStr  = "connected"
	dtlsTransportStateClosedStr     = "closed"
	dtlsTransportStateFailedStr     = "failed"
)

func newDTLSTransportState(raw string) DTLSTransportState {
	switch raw {
	case dtlsTransportStateNewStr:
		return DTLSTransportStateNew
	case dtlsTransportStateConnectingStr:
		return DTLSTransportStateConnecting
	case dtlsTransportStateConnectedStr:
		return DTLSTransportStateConnected
	case dtlsTransportStateClosedStr:
		return DTLSTransportStateClosed
	case dtlsTransportStateFailedStr:
		return DTLSTransportStateFailed
	default:
		return DTLSTransportState(Unknown)
	}
}

func (t DTLSTransportState) String() string {
	switch t {
	case DTLSTransportStateNew:
		return dtlsTransportStateNewStr
	case DTLSTransportStateConnecting:
		return dtlsTransportStateConnectingStr
	case DTLSTransportStateConnected:
		return dtlsTransportStateConnectedStr
	case DTLSTransportStateClosed:
		return dtlsTransportStateClosedStr
	case DTLSTransportStateFailed:
		return dtlsTransportStateFailedStr
	default:
		return ErrUnknownType.Error()
	}
}
