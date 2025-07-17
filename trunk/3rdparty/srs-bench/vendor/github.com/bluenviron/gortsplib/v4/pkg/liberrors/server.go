package liberrors

import (
	"fmt"
	"net"

	"github.com/bluenviron/gortsplib/v4/pkg/headers"
)

// ErrServerTerminated is an error that can be returned by a server.
type ErrServerTerminated = ErrClientTerminated

// ErrServerSessionNotFound is an error that can be returned by a server.
type ErrServerSessionNotFound struct{}

// Error implements the error interface.
func (e ErrServerSessionNotFound) Error() string {
	return "session not found"
}

// ErrServerSessionTimedOut is an error that can be returned by a server.
type ErrServerSessionTimedOut struct{}

// Error implements the error interface.
func (e ErrServerSessionTimedOut) Error() string {
	return "session timed out"
}

// ErrServerCSeqMissing is an error that can be returned by a server.
type ErrServerCSeqMissing struct{}

// Error implements the error interface.
func (e ErrServerCSeqMissing) Error() string {
	return "CSeq is missing"
}

// ErrServerInvalidState is an error that can be returned by a server.
type ErrServerInvalidState struct {
	AllowedList []fmt.Stringer
	State       fmt.Stringer
}

// Error implements the error interface.
func (e ErrServerInvalidState) Error() string {
	return fmt.Sprintf("must be in state %v, while is in state %v",
		e.AllowedList, e.State)
}

// ErrServerInvalidPath is an error that can be returned by a server.
type ErrServerInvalidPath struct{}

// Error implements the error interface.
func (e ErrServerInvalidPath) Error() string {
	return "invalid path"
}

// ErrServerContentTypeMissing is an error that can be returned by a server.
type ErrServerContentTypeMissing = ErrClientContentTypeMissing

// ErrServerContentTypeUnsupported is an error that can be returned by a server.
type ErrServerContentTypeUnsupported = ErrClientContentTypeUnsupported

// ErrServerSDPInvalid is an error that can be returned by a server.
type ErrServerSDPInvalid = ErrClientSDPInvalid

// ErrServerTransportHeaderInvalid is an error that can be returned by a server.
type ErrServerTransportHeaderInvalid = ErrClientTransportHeaderInvalid

// ErrServerMediaAlreadySetup is an error that can be returned by a server.
type ErrServerMediaAlreadySetup struct{}

// Error implements the error interface.
func (e ErrServerMediaAlreadySetup) Error() string {
	return "media has already been setup"
}

// ErrServerMediaNotFound is an error that can be returned by a server.
type ErrServerMediaNotFound struct{}

// Error implements the error interface.
func (e ErrServerMediaNotFound) Error() string {
	return "media not found"
}

// ErrServerTransportHeaderInvalidMode is an error that can be returned by a server.
type ErrServerTransportHeaderInvalidMode struct {
	Mode *headers.TransportMode
}

// Error implements the error interface.
func (e ErrServerTransportHeaderInvalidMode) Error() string {
	m := "null"
	if e.Mode != nil {
		m = e.Mode.String()
	}
	return fmt.Sprintf("transport header contains a invalid mode (%v)", m)
}

// ErrServerTransportHeaderNoClientPorts is an error that can be returned by a server.
type ErrServerTransportHeaderNoClientPorts struct{}

// Error implements the error interface.
func (e ErrServerTransportHeaderNoClientPorts) Error() string {
	return "transport header does not contain client ports"
}

// ErrServerTransportHeaderInvalidInterleavedIDs is an error that can be returned by a server.
type ErrServerTransportHeaderInvalidInterleavedIDs struct{}

// Error implements the error interface.
func (e ErrServerTransportHeaderInvalidInterleavedIDs) Error() string {
	return "invalid interleaved IDs"
}

// ErrServerTransportHeaderInterleavedIDsInUse is an error that can be returned by a server.
type ErrServerTransportHeaderInterleavedIDsInUse struct{}

// Error implements the error interface.
func (e ErrServerTransportHeaderInterleavedIDsInUse) Error() string {
	return "interleaved IDs are in use"
}

// ErrServerMediasDifferentPaths is an error that can be returned by a server.
type ErrServerMediasDifferentPaths struct{}

// Error implements the error interface.
func (e ErrServerMediasDifferentPaths) Error() string {
	return "can't setup medias with different paths"
}

// ErrServerMediasDifferentProtocols is an error that can be returned by a server.
type ErrServerMediasDifferentProtocols struct{}

// Error implements the error interface.
func (e ErrServerMediasDifferentProtocols) Error() string {
	return "can't setup medias with different protocols"
}

// ErrServerNoMediasSetup is an error that can be returned by a server.
type ErrServerNoMediasSetup struct{}

// Error implements the error interface.
func (e ErrServerNoMediasSetup) Error() string {
	return "no medias have been setup"
}

// ErrServerNotAllAnnouncedMediasSetup is an error that can be returned by a server.
type ErrServerNotAllAnnouncedMediasSetup struct{}

// Error implements the error interface.
func (e ErrServerNotAllAnnouncedMediasSetup) Error() string {
	return "not all announced medias have been setup"
}

// ErrServerLinkedToOtherSession is an error that can be returned by a server.
type ErrServerLinkedToOtherSession struct{}

// Error implements the error interface.
func (e ErrServerLinkedToOtherSession) Error() string {
	return "connection is linked to another session"
}

// ErrServerSessionTornDown is an error that can be returned by a server.
type ErrServerSessionTornDown struct {
	Author net.Addr
}

// Error implements the error interface.
func (e ErrServerSessionTornDown) Error() string {
	return fmt.Sprintf("torn down by %v", e.Author)
}

// ErrServerSessionLinkedToOtherConn is an error that can be returned by a server.
type ErrServerSessionLinkedToOtherConn struct{}

// Error implements the error interface.
func (e ErrServerSessionLinkedToOtherConn) Error() string {
	return "session is linked to another connection"
}

// ErrServerInvalidSession is an error that can be returned by a server.
type ErrServerInvalidSession struct{}

// Error implements the error interface.
func (e ErrServerInvalidSession) Error() string {
	return "invalid session"
}

// ErrServerPathHasChanged is an error that can be returned by a server.
type ErrServerPathHasChanged struct {
	Prev string
	Cur  string
}

// Error implements the error interface.
func (e ErrServerPathHasChanged) Error() string {
	return fmt.Sprintf("path has changed, was '%s', now is '%s'", e.Prev, e.Cur)
}

// ErrServerCannotUseSessionCreatedByOtherIP is an error that can be returned by a server.
type ErrServerCannotUseSessionCreatedByOtherIP struct{}

// Error implements the error interface.
func (e ErrServerCannotUseSessionCreatedByOtherIP) Error() string {
	return "cannot use a session created with a different IP"
}

// ErrServerUDPPortsAlreadyInUse is an error that can be returned by a server.
type ErrServerUDPPortsAlreadyInUse struct {
	Port int
}

// Error implements the error interface.
func (e ErrServerUDPPortsAlreadyInUse) Error() string {
	return fmt.Sprintf("UDP ports %d and %d are already assigned to another reader with the same IP",
		e.Port, e.Port+1)
}

// ErrServerSessionNotInUse is an error that can be returned by a server.
type ErrServerSessionNotInUse struct{}

// Error implements the error interface.
func (e ErrServerSessionNotInUse) Error() string {
	return "not in use"
}

// ErrServerUnexpectedFrame is an error that can be returned by a server.
type ErrServerUnexpectedFrame = ErrClientUnexpectedFrame

// ErrServerUnexpectedResponse is an error that can be returned by a server.
type ErrServerUnexpectedResponse struct{}

// Error implements the error interface.
func (e ErrServerUnexpectedResponse) Error() string {
	return "received unexpected response"
}

// ErrServerWriteQueueFull is an error that can be returned by a server.
type ErrServerWriteQueueFull = ErrClientWriteQueueFull

// ErrServerRTPPacketsLost is an error that can be returned by a server.
//
// Deprecated: will be removed in next version.
type ErrServerRTPPacketsLost = ErrClientRTPPacketsLost

// ErrServerRTPPacketUnknownPayloadType is an error that can be returned by a server.
type ErrServerRTPPacketUnknownPayloadType = ErrClientRTPPacketUnknownPayloadType

// ErrServerRTCPPacketTooBig is an error that can be returned by a server.
type ErrServerRTCPPacketTooBig = ErrClientRTCPPacketTooBig

// ErrServerRTPPacketTooBigUDP is an error that can be returned by a server.
type ErrServerRTPPacketTooBigUDP = ErrClientRTPPacketTooBigUDP

// ErrServerRTCPPacketTooBigUDP is an error that can be returned by a server.
type ErrServerRTCPPacketTooBigUDP = ErrClientRTCPPacketTooBigUDP

// ErrServerStreamClosed is an error that can be returned by a server.
type ErrServerStreamClosed struct{}

// Error implements the error interface.
func (e ErrServerStreamClosed) Error() string {
	return "stream is closed"
}

// ErrServerInvalidSetupPath is an error that can be returned by a server.
type ErrServerInvalidSetupPath struct{}

// Error implements the error interface.
func (ErrServerInvalidSetupPath) Error() string {
	return "invalid SETUP path. " +
		"This typically happens when VLC fails a request, and then switches to an " +
		"unsupported RTSP dialect"
}

// ErrServerAuth is an error that can be returned by a server.
// If a client did not provide credentials, it will be asked for
// credentials instead of being kicked out.
type ErrServerAuth struct{}

// Error implements the error interface.
func (e ErrServerAuth) Error() string {
	return "authentication error"
}
