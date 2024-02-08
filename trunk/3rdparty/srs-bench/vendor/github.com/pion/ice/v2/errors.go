// SPDX-FileCopyrightText: 2023 The Pion community <https://pion.ly>
// SPDX-License-Identifier: MIT

package ice

import "errors"

var (
	// ErrUnknownType indicates an error with Unknown info.
	ErrUnknownType = errors.New("Unknown")

	// ErrSchemeType indicates the scheme type could not be parsed.
	ErrSchemeType = errors.New("unknown scheme type")

	// ErrSTUNQuery indicates query arguments are provided in a STUN URL.
	ErrSTUNQuery = errors.New("queries not supported in STUN address")

	// ErrInvalidQuery indicates an malformed query is provided.
	ErrInvalidQuery = errors.New("invalid query")

	// ErrHost indicates malformed hostname is provided.
	ErrHost = errors.New("invalid hostname")

	// ErrPort indicates malformed port is provided.
	ErrPort = errors.New("invalid port")

	// ErrLocalUfragInsufficientBits indicates local username fragment insufficient bits are provided.
	// Have to be at least 24 bits long
	ErrLocalUfragInsufficientBits = errors.New("local username fragment is less than 24 bits long")

	// ErrLocalPwdInsufficientBits indicates local password insufficient bits are provided.
	// Have to be at least 128 bits long
	ErrLocalPwdInsufficientBits = errors.New("local password is less than 128 bits long")

	// ErrProtoType indicates an unsupported transport type was provided.
	ErrProtoType = errors.New("invalid transport protocol type")

	// ErrClosed indicates the agent is closed
	ErrClosed = errors.New("the agent is closed")

	// ErrNoCandidatePairs indicates agent does not have a valid candidate pair
	ErrNoCandidatePairs = errors.New("no candidate pairs available")

	// ErrCanceledByCaller indicates agent connection was canceled by the caller
	ErrCanceledByCaller = errors.New("connecting canceled by caller")

	// ErrMultipleStart indicates agent was started twice
	ErrMultipleStart = errors.New("attempted to start agent twice")

	// ErrRemoteUfragEmpty indicates agent was started with an empty remote ufrag
	ErrRemoteUfragEmpty = errors.New("remote ufrag is empty")

	// ErrRemotePwdEmpty indicates agent was started with an empty remote pwd
	ErrRemotePwdEmpty = errors.New("remote pwd is empty")

	// ErrNoOnCandidateHandler indicates agent was started without OnCandidate
	ErrNoOnCandidateHandler = errors.New("no OnCandidate provided")

	// ErrMultipleGatherAttempted indicates GatherCandidates has been called multiple times
	ErrMultipleGatherAttempted = errors.New("attempting to gather candidates during gathering state")

	// ErrUsernameEmpty indicates agent was give TURN URL with an empty Username
	ErrUsernameEmpty = errors.New("username is empty")

	// ErrPasswordEmpty indicates agent was give TURN URL with an empty Password
	ErrPasswordEmpty = errors.New("password is empty")

	// ErrAddressParseFailed indicates we were unable to parse a candidate address
	ErrAddressParseFailed = errors.New("failed to parse address")

	// ErrLiteUsingNonHostCandidates indicates non host candidates were selected for a lite agent
	ErrLiteUsingNonHostCandidates = errors.New("lite agents must only use host candidates")

	// ErrUselessUrlsProvided indicates that one or more URL was provided to the agent but no host
	// candidate required them
	ErrUselessUrlsProvided = errors.New("agent does not need URL with selected candidate types")

	// ErrUnsupportedNAT1To1IPCandidateType indicates that the specified NAT1To1IPCandidateType is
	// unsupported
	ErrUnsupportedNAT1To1IPCandidateType = errors.New("unsupported 1:1 NAT IP candidate type")

	// ErrInvalidNAT1To1IPMapping indicates that the given 1:1 NAT IP mapping is invalid
	ErrInvalidNAT1To1IPMapping = errors.New("invalid 1:1 NAT IP mapping")

	// ErrExternalMappedIPNotFound in NAT1To1IPMapping
	ErrExternalMappedIPNotFound = errors.New("external mapped IP not found")

	// ErrMulticastDNSWithNAT1To1IPMapping indicates that the mDNS gathering cannot be used along
	// with 1:1 NAT IP mapping for host candidate.
	ErrMulticastDNSWithNAT1To1IPMapping = errors.New("mDNS gathering cannot be used with 1:1 NAT IP mapping for host candidate")

	// ErrIneffectiveNAT1To1IPMappingHost indicates that 1:1 NAT IP mapping for host candidate is
	// requested, but the host candidate type is disabled.
	ErrIneffectiveNAT1To1IPMappingHost = errors.New("1:1 NAT IP mapping for host candidate ineffective")

	// ErrIneffectiveNAT1To1IPMappingSrflx indicates that 1:1 NAT IP mapping for srflx candidate is
	// requested, but the srflx candidate type is disabled.
	ErrIneffectiveNAT1To1IPMappingSrflx = errors.New("1:1 NAT IP mapping for srflx candidate ineffective")

	// ErrInvalidMulticastDNSHostName indicates an invalid MulticastDNSHostName
	ErrInvalidMulticastDNSHostName = errors.New("invalid mDNS HostName, must end with .local and can only contain a single '.'")

	// ErrRunCanceled indicates a run operation was canceled by its individual done
	ErrRunCanceled = errors.New("run was canceled by done")

	// ErrTCPRemoteAddrAlreadyExists indicates we already have the connection with same remote addr.
	ErrTCPRemoteAddrAlreadyExists = errors.New("conn with same remote addr already exists")

	// ErrUnknownCandidateTyp indicates that a candidate had a unknown type value.
	ErrUnknownCandidateTyp = errors.New("unknown candidate typ")

	// ErrDetermineNetworkType indicates that the NetworkType was not able to be parsed
	ErrDetermineNetworkType = errors.New("unable to determine networkType")

	errSendPacket                    = errors.New("failed to send packet")
	errAttributeTooShortICECandidate = errors.New("attribute not long enough to be ICE candidate")
	errParseComponent                = errors.New("could not parse component")
	errParsePriority                 = errors.New("could not parse priority")
	errParsePort                     = errors.New("could not parse port")
	errParseRelatedAddr              = errors.New("could not parse related addresses")
	errParseTCPType                  = errors.New("could not parse TCP type")
	errGetXorMappedAddrResponse      = errors.New("failed to get XOR-MAPPED-ADDRESS response")
	errConnectionAddrAlreadyExist    = errors.New("connection with same remote address already exists")
	errReadingStreamingPacket        = errors.New("error reading streaming packet")
	errWriting                       = errors.New("error writing to")
	errClosingConnection             = errors.New("error closing connection")
	errRead                          = errors.New("unexpected error trying to read")
	errUnknownRole                   = errors.New("unknown role")
	errICEWriteSTUNMessage           = errors.New("the ICE conn can't write STUN messages")
	errUDPMuxDisabled                = errors.New("UDPMux is not enabled")
	errNoXorAddrMapping              = errors.New("no address mapping")
	errSendSTUNPacket                = errors.New("failed to send STUN packet")
	errXORMappedAddrTimeout          = errors.New("timeout while waiting for XORMappedAddr")
	errNotImplemented                = errors.New("not implemented yet")
	errNoUDPMuxAvailable             = errors.New("no UDP mux is available")
	errNoTCPMuxAvailable             = errors.New("no TCP mux is available")
	errInvalidAddress                = errors.New("invalid address")

	// UDPMuxDefault should not listen on unspecified address, but to keep backward compatibility, don't return error now.
	// will be used in the future.
	// errListenUnspecified             = errors.New("can't listen on unspecified address")
)
