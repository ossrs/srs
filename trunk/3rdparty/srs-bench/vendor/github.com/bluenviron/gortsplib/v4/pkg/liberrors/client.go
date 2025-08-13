package liberrors

import (
	"fmt"

	"github.com/bluenviron/gortsplib/v4/pkg/base"
)

// ErrClientTerminated is an error that can be returned by a client.
type ErrClientTerminated struct{}

// Error implements the error interface.
func (e ErrClientTerminated) Error() string {
	return "terminated"
}

// ErrClientInvalidState is an error that can be returned by a client.
type ErrClientInvalidState struct {
	AllowedList []fmt.Stringer
	State       fmt.Stringer
}

// Error implements the error interface.
func (e ErrClientInvalidState) Error() string {
	return fmt.Sprintf("must be in state %v, while is in state %v",
		e.AllowedList, e.State)
}

// ErrClientSessionHeaderInvalid is an error that can be returned by a client.
type ErrClientSessionHeaderInvalid struct {
	Err error
}

// Error implements the error interface.
func (e ErrClientSessionHeaderInvalid) Error() string {
	return fmt.Sprintf("invalid session header: %v", e.Err)
}

// ErrClientBadStatusCode is an error that can be returned by a client.
type ErrClientBadStatusCode struct {
	Code    base.StatusCode
	Message string
}

// Error implements the error interface.
func (e ErrClientBadStatusCode) Error() string {
	return fmt.Sprintf("bad status code: %d (%s)", e.Code, e.Message)
}

// ErrClientContentTypeMissing is an error that can be returned by a client.
type ErrClientContentTypeMissing struct{}

// Error implements the error interface.
func (e ErrClientContentTypeMissing) Error() string {
	return "Content-Type header is missing"
}

// ErrClientContentTypeUnsupported is an error that can be returned by a client.
type ErrClientContentTypeUnsupported struct {
	CT base.HeaderValue
}

// Error implements the error interface.
func (e ErrClientContentTypeUnsupported) Error() string {
	return fmt.Sprintf("unsupported Content-Type header '%v'", e.CT)
}

// ErrClientCannotSetupMediasDifferentURLs is an error that can be returned by a client.
type ErrClientCannotSetupMediasDifferentURLs struct{}

// Error implements the error interface.
func (e ErrClientCannotSetupMediasDifferentURLs) Error() string {
	return "cannot setup medias with different base URLs"
}

// ErrClientUDPPortsZero is an error that can be returned by a client.
type ErrClientUDPPortsZero struct{}

// Error implements the error interface.
func (e ErrClientUDPPortsZero) Error() string {
	return "rtpPort and rtcpPort must be both zero or non-zero"
}

// ErrClientUDPPortsNotConsecutive is an error that can be returned by a client.
type ErrClientUDPPortsNotConsecutive struct{}

// Error implements the error interface.
func (e ErrClientUDPPortsNotConsecutive) Error() string {
	return "rtcpPort must be rtpPort + 1"
}

// ErrClientServerPortsNotProvided is an error that can be returned by a client.
type ErrClientServerPortsNotProvided struct{}

// Error implements the error interface.
func (e ErrClientServerPortsNotProvided) Error() string {
	return "server ports have not been provided. Use AnyPortEnable to communicate with this server"
}

// ErrClientTransportHeaderInvalid is an error that can be returned by a client.
type ErrClientTransportHeaderInvalid struct {
	Err error
}

// Error implements the error interface.
func (e ErrClientTransportHeaderInvalid) Error() string {
	return fmt.Sprintf("invalid transport header: %v", e.Err)
}

// ErrClientServerRequestedTCP is an error that can be returned by a client.
type ErrClientServerRequestedTCP struct{}

// Error implements the error interface.
func (e ErrClientServerRequestedTCP) Error() string {
	return "server wants to use the TCP transport protocol"
}

// ErrClientServerRequestedUDP is an error that can be returned by a client.
type ErrClientServerRequestedUDP struct{}

// Error implements the error interface.
func (e ErrClientServerRequestedUDP) Error() string {
	return "server wants to use the UDP transport protocol"
}

// ErrClientTransportHeaderInvalidDelivery is an error that can be returned by a client.
type ErrClientTransportHeaderInvalidDelivery struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderInvalidDelivery) Error() string {
	return "transport header contains an invalid delivery value"
}

// ErrClientTransportHeaderNoPorts is an error that can be returned by a client.
type ErrClientTransportHeaderNoPorts struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderNoPorts) Error() string {
	return "transport header does not contain ports"
}

// ErrClientTransportHeaderNoDestination is an error that can be returned by a client.
type ErrClientTransportHeaderNoDestination struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderNoDestination) Error() string {
	return "transport header does not contain a destination"
}

// ErrClientTransportHeaderNoInterleavedIDs is an error that can be returned by a client.
type ErrClientTransportHeaderNoInterleavedIDs struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderNoInterleavedIDs) Error() string {
	return "transport header does not contain interleaved IDs"
}

// ErrClientTransportHeaderInvalidInterleavedIDs is an error that can be returned by a client.
type ErrClientTransportHeaderInvalidInterleavedIDs struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderInvalidInterleavedIDs) Error() string {
	return "invalid interleaved IDs"
}

// ErrClientTransportHeaderInterleavedIDsInUse is an error that can be returned by a client.
type ErrClientTransportHeaderInterleavedIDsInUse struct{}

// Error implements the error interface.
func (e ErrClientTransportHeaderInterleavedIDsInUse) Error() string {
	return "interleaved IDs are in use"
}

// ErrClientUDPTimeout is an error that can be returned by a client.
type ErrClientUDPTimeout struct{}

// Error implements the error interface.
func (e ErrClientUDPTimeout) Error() string {
	return "UDP timeout"
}

// ErrClientTCPTimeout is an error that can be returned by a client.
type ErrClientTCPTimeout struct{}

// Error implements the error interface.
func (e ErrClientTCPTimeout) Error() string {
	return "TCP timeout"
}

// ErrClientRTPInfoInvalid is an error that can be returned by a client.
type ErrClientRTPInfoInvalid struct {
	Err error
}

// Error implements the error interface.
func (e ErrClientRTPInfoInvalid) Error() string {
	return fmt.Sprintf("invalid RTP-Info: %v", e.Err)
}

// ErrClientUnexpectedFrame is an error that can be returned by a client.
type ErrClientUnexpectedFrame struct{}

// Error implements the error interface.
func (e ErrClientUnexpectedFrame) Error() string {
	return "received unexpected interleaved frame"
}

// ErrClientRequestTimedOut is an error that can be returned by a client.
type ErrClientRequestTimedOut struct{}

// Error implements the error interface.
func (e ErrClientRequestTimedOut) Error() string {
	return "request timed out"
}

// ErrClientUnsupportedScheme is an error that can be returned by a client.
type ErrClientUnsupportedScheme struct {
	Scheme string
}

// Error implements the error interface.
func (e ErrClientUnsupportedScheme) Error() string {
	return fmt.Sprintf("unsupported scheme: %v", e.Scheme)
}

// ErrClientRTSPSTCP is an error that can be returned by a client.
type ErrClientRTSPSTCP struct{}

// Error implements the error interface.
func (e ErrClientRTSPSTCP) Error() string {
	return "RTSPS can be used only with TCP"
}

// ErrClientUnhandledMethod is an error that can be returned by a client.
type ErrClientUnhandledMethod struct {
	Method base.Method
}

// Error implements the error interface.
func (e ErrClientUnhandledMethod) Error() string {
	return fmt.Sprintf("unhandled method: %v", e.Method)
}

// ErrClientWriteQueueFull is an error that can be returned by a client.
type ErrClientWriteQueueFull struct{}

// Error implements the error interface.
func (e ErrClientWriteQueueFull) Error() string {
	return "write queue is full"
}

// ErrClientRTPPacketsLost is an error that can be returned by a client.
//
// Deprecated: will be removed in next version.
type ErrClientRTPPacketsLost struct {
	Lost uint
}

// Error implements the error interface.
func (e ErrClientRTPPacketsLost) Error() string {
	return fmt.Sprintf("%d RTP %s lost",
		e.Lost,
		func() string {
			if e.Lost == 1 {
				return "packet"
			}
			return "packets"
		}())
}

// ErrClientRTPPacketUnknownPayloadType is an error that can be returned by a client.
type ErrClientRTPPacketUnknownPayloadType struct {
	PayloadType uint8
}

// Error implements the error interface.
func (e ErrClientRTPPacketUnknownPayloadType) Error() string {
	return fmt.Sprintf("received RTP packet with unknown payload type: %d", e.PayloadType)
}

// ErrClientRTCPPacketTooBig is an error that can be returned by a client.
type ErrClientRTCPPacketTooBig struct {
	L   int
	Max int
}

// Error implements the error interface.
func (e ErrClientRTCPPacketTooBig) Error() string {
	return fmt.Sprintf("RTCP packet size (%d) is greater than maximum allowed (%d)",
		e.L, e.Max)
}

// ErrClientRTPPacketTooBigUDP is an error that can be returned by a client.
type ErrClientRTPPacketTooBigUDP struct{}

// Error implements the error interface.
func (e ErrClientRTPPacketTooBigUDP) Error() string {
	return "RTP packet is too big to be read with UDP"
}

// ErrClientRTCPPacketTooBigUDP is an error that can be returned by a client.
type ErrClientRTCPPacketTooBigUDP struct{}

// Error implements the error interface.
func (e ErrClientRTCPPacketTooBigUDP) Error() string {
	return "RTCP packet is too big to be read with UDP"
}

// ErrClientSwitchToTCP is an error that can be returned by a client.
type ErrClientSwitchToTCP struct{}

// Error implements the error interface.
func (e ErrClientSwitchToTCP) Error() string {
	return "no UDP packets received, switching to TCP"
}

// ErrClientSwitchToTCP2 is an error that can be returned by a client.
type ErrClientSwitchToTCP2 struct{}

// Error implements the error interface.
func (e ErrClientSwitchToTCP2) Error() string {
	return "switching to TCP because server requested it"
}

// ErrClientAuthSetup is an error that can be returned by a client.
type ErrClientAuthSetup struct {
	Err error
}

// Error implements the error interface.
func (e ErrClientAuthSetup) Error() string {
	return fmt.Sprintf("unable to setup authentication: %s", e.Err)
}

// ErrClientSDPInvalid is an error that can be returned by a client.
type ErrClientSDPInvalid struct {
	Err error
}

// Error implements the error interface.
func (e ErrClientSDPInvalid) Error() string {
	return fmt.Sprintf("invalid SDP: %v", e.Err)
}
